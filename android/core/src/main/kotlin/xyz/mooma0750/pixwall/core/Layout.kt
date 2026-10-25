package xyz.mooma0750.pixwall.core

import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** 並べ方。設定の JSON にもこの名前で書く（Mac・Windows とそろえる） */
enum class Fit(val json: String) {
    /** 整数倍で最大（既定）。整数倍で収まらない大きな画像は FIT と同じ */
    INTEGER("integer"),

    /** 画面に合わせる（はみ出さない） */
    FIT("fit"),

    /** 画面を埋める（はみ出す） */
    FILL("fill"),

    /** 自由に置く（大きさと横・縦の位置を自分で決める） */
    FREE("free"),
    ;

    companion object {
        fun fromJson(s: String?): Fit? = entries.firstOrNull { it.json == s }
    }
}

/** 自由に置くときの大きさの範囲（画面に合わせたときの大きさを 1 とする） */
val FREE_SIZE_RANGE = 0.1..3.0

/** 並べ方と、それぞれの並べ方で使う位置・大きさ（Mac の Placement と同じ）。設定の JSON では Config の同じ名前の項目に書く */
data class Placement(
    val fit: Fit = Fit.INTEGER,
    /** 画面に合わせる・埋めるときの位置（0 で上・左、0.5 で中央、1 で下・右） */
    val position: Double = 0.5,
    /** 自由に置くときの横の位置（0 で画像の左の端を画面の左の端に、1 で右の端を右の端にそろえる） */
    val freeX: Double = 0.5,
    /** 自由に置くときの縦の位置（0 で上の端、1 で下の端をそろえる） */
    val freeY: Double = 0.5,
    /** 自由に置くときの大きさ。画面に合わせたときの大きさを 1 とする */
    val freeSize: Double = 1.0,
)

/** 物理ピクセルでの大きさと位置 */
data class PixelRect(val x: Int, val y: Int, val width: Int, val height: Int)

/**
 * 画像を画面のどこにどの大きさで置くかを、物理ピクセルで決める（Mac の `layout` と同じ計算）。
 * fit と fill では、余白（fill でははみ出し）の中のどこに置くかを position で決める（0 で上・左、0.5 で中央、1 で下・右）
 */
fun layout(
    imageWidth: Int,
    imageHeight: Int,
    screenWidth: Int,
    screenHeight: Int,
    fit: Fit,
    position: Double = 0.5,
): PixelRect = layout(imageWidth, imageHeight, screenWidth, screenHeight, Placement(fit, position))

/** FREE では、画面に合わせたときの大きさに freeSize を掛け、横は freeX、縦は freeY で置く（FILL の position と同じ決まり） */
fun layout(
    imageWidth: Int,
    imageHeight: Int,
    screenWidth: Int,
    screenHeight: Int,
    placement: Placement,
): PixelRect {
    val fit = placement.fit
    if (imageWidth <= 0 || imageHeight <= 0 || screenWidth <= 0 || screenHeight <= 0) return PixelRect(0, 0, 0, 0)
    val sx = screenWidth.toDouble() / imageWidth
    val sy = screenHeight.toDouble() / imageHeight

    val (width, height) =
        when {
            fit == Fit.INTEGER && min(sx, sy) >= 1 -> {
                val scale = min(screenWidth / imageWidth, screenHeight / imageHeight)
                imageWidth * scale to imageHeight * scale
            }
            fit == Fit.FILL -> scaled(imageWidth, imageHeight, max(sx, sy))
            fit == Fit.FREE -> scaled(imageWidth, imageHeight, min(sx, sy) * placement.freeSize.coerceIn(FREE_SIZE_RANGE))
            else -> scaled(imageWidth, imageHeight, min(sx, sy))
        }
    val (px, py) =
        when (fit) {
            Fit.INTEGER -> 0.5 to 0.5
            Fit.FREE -> placement.freeX to placement.freeY
            else -> placement.position to placement.position
        }
    // 余白×位置を 0 の側へ切り捨てる（はみ出す負の余白でも）
    fun place(slack: Int, p: Double) = (slack * p.coerceIn(0.0, 1.0)).toInt()
    return PixelRect(place(screenWidth - width, px), place(screenHeight - height, py), width, height)
}

private fun scaled(w: Int, h: Int, scale: Double): Pair<Int, Int> =
    max(1, (w * scale).roundToInt()) to max(1, (h * scale).roundToInt())

/** 速さの倍率の5段 */
val SPEEDS = listOf(0.25, 0.5, 1.0, 2.0, 4.0)

/**
 * GIF に書かれた待ち時間（ミリ秒）を、実際に待つ時間にする。
 * 10ms 以下はブラウザと同じく 100ms。速さの倍率は 0.25〜4 に収める
 */
fun frameDelayMillis(writtenMillis: Int, speed: Double = 1.0): Long {
    val delay = if (writtenMillis <= 10) 100 else writtenMillis
    return max(1L, (delay / speed.coerceIn(0.25, 4.0)).roundToInt().toLong())
}
