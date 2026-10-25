package xyz.mooma0750.pixwall.core

import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** 文字盤の座標の大きさ（Wear OS の大きい丸の画面の画素の数）。ほかの大きさの時計では OS が拡大・縮小する */
const val WATCH_FACE_SIZE = 454

/**
 * 文字盤に入れる絵。gif は並べ方どおりに拡大して、文字盤に見える範囲だけを切り出したもの（WFF は絵をぼかして拡大するので、
 * こちらでニアレストネイバーで拡大しておく）。rect はその置き場所、background は余白の色（0xRRGGBB）、
 * preview は文字盤の一覧に出す1枚目（size×size の ARGB）
 */
class WatchFaceArt(
    val gif: ByteArray,
    val rect: PixelRect,
    val background: Int,
    val preview: IntArray,
    val size: Int,
    val frameCount: Int,
) {
    /** Play の審査と同じ数え方のメモリ（幅×高さ×4バイト×枚数）。動いている間の上限は 100MB */
    val memoryBytes: Long get() = rect.width.toLong() * rect.height * 4 * frameCount
}

/** Play の審査の、動いている間のメモリの上限 */
const val WATCH_FACE_MEMORY_LIMIT = 100L * 1000 * 1000

/** 並べ方どおりに置いたときの、見える範囲（はみ出すところは書かない）と、その範囲の各列・各行が元の絵のどこか */
private class Plan(gif: GifImage, config: Config, val size: Int) {
    val background = config.customColor ?: (gif.frames[0][0] and 0xFFFFFF)
    private val sourceWidth = gif.width
    val rect: PixelRect
    private val columns: IntArray
    private val rows: IntArray

    init {
        val r = layout(gif.width, gif.height, size, size, config.placement)
        val left = max(r.x, 0)
        val top = max(r.y, 0)
        rect = PixelRect(left, top, min(r.x + r.width, size) - left, min(r.y + r.height, size) - top)
        columns = IntArray(rect.width) { (((left + it - r.x).toLong() * gif.width) / r.width).toInt().coerceIn(0, gif.width - 1) }
        rows = IntArray(rect.height) { (((top + it - r.y).toLong() * gif.height) / r.height).toInt().coerceIn(0, gif.height - 1) }
    }

    /** 1枚を見える範囲の大きさにニアレストネイバーで拡大する。透明なところは余白の色（0xRRGGBB で返す） */
    fun scaled(frame: IntArray): IntArray {
        val w = rect.width
        val out = IntArray(w * rect.height)
        for (y in rows.indices) {
            val row = rows[y] * sourceWidth
            for (x in 0 until w) {
                val c = frame[row + columns[x]]
                out[y * w + x] = if (c ushr 24 == 0) background else c and 0xFFFFFF
            }
        }
        return out
    }

    /** 文字盤の全体（size×size の ARGB）。余白を塗り、見える範囲に picture を置く */
    fun whole(picture: IntArray): IntArray {
        val out = IntArray(size * size) { 0xFF000000.toInt() or background }
        for (y in 0 until rect.height) {
            for (x in 0 until rect.width) {
                out[(rect.y + y) * size + rect.x + x] = 0xFF000000.toInt() or picture[y * rect.width + x]
            }
        }
        return out
    }
}

/** 文字盤の1枚目の見本（size×size の ARGB）。GIF を書かないので、設定を変えている間に何度でも作れる */
fun previewWatchFace(gif: GifImage, config: Config, size: Int = WATCH_FACE_SIZE): IntArray {
    val plan = Plan(gif, config, size)
    return plan.whole(plan.scaled(gif.frames[0]))
}

/**
 * GIF を設定の並べ方・位置・余白の色・速さで文字盤の絵にする。拡大はニアレストネイバーで、透明なところは余白の色で埋める。
 * 続く同じ絵のフレームはまとめる（待ち時間を足す）
 */
fun renderWatchFace(gif: GifImage, config: Config, size: Int = WATCH_FACE_SIZE): WatchFaceArt {
    val source = gif.mergingRepeats()
    val plan = Plan(source, config, size)

    // 余白の色で埋めると同じ絵になるフレームもあるので、拡大したあとでもう一度まとめる
    val frames = ArrayList<IntArray>()
    val delays = ArrayList<Int>()
    source.frames.forEachIndexed { i, frame ->
        val s = plan.scaled(frame)
        val delay = frameDelayMillis(source.delaysMillis[i], config.speed).toInt()
        if (frames.isNotEmpty() && frames.last().contentEquals(s)) {
            delays[delays.lastIndex] += delay
        } else {
            frames += s
            delays += delay
        }
    }

    val encoder = GifEncoder(plan.rect.width, plan.rect.height)
    frames.forEachIndexed { i, f -> encoder.addFrame(f, centis(delays[i])) }
    return WatchFaceArt(encoder.finish(), plan.rect, plan.background, plan.whole(frames[0]), size, frames.size)
}

/** GIF の待ち時間は 1/100 秒で書く。1 枚だけの GIF は 0。10ms 以下はブラウザと同じく 100ms と読まれるので 2（20ms）より短くしない */
private fun centis(millis: Int): Int = max(2, (millis / 10.0).roundToInt())

/**
 * 文字盤の XML（Watch Face Format 1）。絵を rect に置き、後ろを余白の色で塗り、時刻を重ねる。
 * アンビエントでは絵を消し、黒地に細い時刻だけにする（焼き付きと電池のため）。地を黒にしておくのは、
 * 余白の色にするとアンビエントで丸のふちに色が残るため
 */
fun watchFaceXml(art: WatchFaceArt): String {
    val s = art.size
    val r = art.rect
    val bg = "#ff%06x".format(art.background)
    val clockY = (s - 100) / 2
    return """
        |<?xml version="1.0"?>
        |<WatchFace width="$s" height="$s">
        |  <Metadata key="CLOCK_TYPE" value="DIGITAL"/>
        |  <Metadata key="PREVIEW_TIME" value="10:08:32"/>
        |  <Scene backgroundColor="#ff000000">
        |    <Group name="picture" x="0" y="0" width="$s" height="$s">
        |      <Variant mode="AMBIENT" target="alpha" value="0"/>
        |      <PartDraw name="margin" x="0" y="0" width="$s" height="$s">
        |        <Rectangle x="0" y="0" width="$s" height="$s">
        |          <Fill color="$bg"/>
        |        </Rectangle>
        |      </PartDraw>
        |      <PartAnimatedImage name="gif" x="${r.x}" y="${r.y}" width="${r.width}" height="${r.height}">
        |        <AnimationController play="ON_VISIBLE" repeat="TRUE" loopCount="0" beforePlaying="FIRST_FRAME"/>
        |        <AnimatedImage resource="pixwall" format="AGIF"/>
        |        <Thumbnail resource="preview"/>
        |      </PartAnimatedImage>
        |    </Group>
        |    <DigitalClock x="0" y="0" width="$s" height="$s">
        |      <TimeText format="hh:mm" hourFormat="SYNC_TO_DEVICE" align="CENTER" x="0" y="$clockY" width="$s" height="100">
        |        <Variant mode="AMBIENT" target="alpha" value="0"/>
        |        <Font family="SYNC_TO_DEVICE" size="96" weight="BOLD" color="#ffffffff"/>
        |      </TimeText>
        |      <TimeText format="hh:mm" hourFormat="SYNC_TO_DEVICE" align="CENTER" x="0" y="$clockY" width="$s" height="100" alpha="0">
        |        <Variant mode="AMBIENT" target="alpha" value="255"/>
        |        <Font family="SYNC_TO_DEVICE" size="96" weight="THIN" color="#ffffffff"/>
        |      </TimeText>
        |    </DigitalClock>
        |  </Scene>
        |</WatchFace>
        |
    """.trimMargin()
}
