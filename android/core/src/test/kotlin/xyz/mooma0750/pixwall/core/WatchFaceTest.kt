package xyz.mooma0750.pixwall.core

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.Random
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

private const val OPAQUE = 0xFF000000.toInt()

class WatchFaceTest {
    private fun opaque(rgb: IntArray) = IntArray(rgb.size) { OPAQUE or rgb[it] }

    // 書いた GIF を自分の読み込みで読むと、同じ絵と待ち時間に戻る（違うところだけ書く2枚目も）
    @Test
    fun encodedGifRoundTrips() {
        val e = GifEncoder(3, 2)
        val a = intArrayOf(0x112233, 0x445566, 0x778899, 0x112233, 0xAABBCC, 0xDDEEFF)
        val b = a.copyOf().also { it[4] = 0x000000 }
        e.addFrame(a, 7)
        e.addFrame(b, 12)
        val gif = decodeGif(e.finish())
        assertEquals(3, gif.width)
        assertEquals(2, gif.height)
        assertArrayEquals(opaque(a), gif.frames[0])
        assertArrayEquals(opaque(b), gif.frames[1])
        assertEquals(listOf(70, 120), gif.delaysMillis)
    }

    // 表が 4096 で埋まって作り直すほど長い絵（256 色）でも戻る
    @Test
    fun longRandomFrameRoundTrips() {
        val random = Random(1)
        val palette = IntArray(256) { random.nextInt(0x1000000) }
        val pixels = IntArray(200 * 150) { palette[random.nextInt(256)] }
        val e = GifEncoder(200, 150)
        e.addFrame(pixels, 10)
        assertArrayEquals(opaque(pixels), decodeGif(e.finish()).frames[0])
    }

    // 256 色を超えたら、多い色を残してほかは近い色にする
    @Test
    fun tooManyColorsFallBackToNearest() {
        val pixels = IntArray(300) { it * 0x10101 and 0xFFFFFF } + IntArray(100) { 0x0000FF }
        val e = GifEncoder(400, 1)
        e.addFrame(pixels, 10)
        val frame = decodeGif(e.finish()).frames[0]
        assertEquals(OPAQUE or 0x0000FF, frame[399])
        assertTrue(frame.toSet().size <= 256)
    }

    private fun gif(width: Int, height: Int, vararg frames: IntArray, delays: List<Int> = frames.map { 100 }) =
        GifImage(width, height, frames.map { opaque(it) }, delays)

    // 整数倍で最大: 64×64 は 7 倍の 448×448 で真ん中（3, 3）。余白の色は1枚目の左上
    @Test
    fun integerScaleIsCenteredWithAutoBackground() {
        val pixels = IntArray(64 * 64) { if (it == 0) 0xF05050 else 0x141838 }
        val art = renderWatchFace(gif(64, 64, pixels), Config())
        assertEquals(PixelRect(3, 3, 448, 448), art.rect)
        assertEquals(0xF05050, art.background)
        val decoded = decodeGif(art.gif)
        assertEquals(448, decoded.width)
        // 左上の1ドットは 7×7 になる
        assertEquals(OPAQUE or 0xF05050, decoded.frames[0][6 * 448 + 6])
        assertEquals(OPAQUE or 0x141838, decoded.frames[0][7 * 448 + 7])
        assertEquals(448L * 448 * 4, art.memoryBytes)
    }

    // 画面を埋める: 見える 454×454 だけを書き、位置で切り出す範囲が動く
    @Test
    fun fillCropsToVisibleArea() {
        // 横長 4×2: 左半分が赤、右半分が青
        val pixels = intArrayOf(0xFF0000, 0xFF0000, 0x0000FF, 0x0000FF, 0xFF0000, 0xFF0000, 0x0000FF, 0x0000FF)
        val left = renderWatchFace(gif(4, 2, pixels), Config(fit = Fit.FILL, position = 0.0))
        assertEquals(PixelRect(0, 0, 454, 454), left.rect)
        // 908×454 に拡大し、左端にそろえるので、見えるのは赤だけ
        assertEquals(setOf(OPAQUE or 0xFF0000), decodeGif(left.gif).frames[0].toSet())
        val right = renderWatchFace(gif(4, 2, pixels), Config(fit = Fit.FILL, position = 1.0))
        assertEquals(setOf(OPAQUE or 0x0000FF), decodeGif(right.gif).frames[0].toSet())
    }

    // 画面に合わせる: 位置 0 で上にそろえ、透明なところは余白の色で埋める
    @Test
    fun fitPlacesByPositionAndFillsTransparency() {
        val art = renderWatchFace(
            GifImage(2, 1, listOf(intArrayOf(OPAQUE or 0x00FF00, 0)), listOf(100)),
            Config(fit = Fit.FIT, position = 0.0, customBackground = true, backgroundColor = 0x123456),
        )
        assertEquals(PixelRect(0, 0, 454, 227), art.rect)
        val frame = decodeGif(art.gif).frames[0]
        assertEquals(OPAQUE or 0x00FF00, frame[0])
        assertEquals(OPAQUE or 0x123456, frame[453])
        assertEquals(OPAQUE or 0x123456, art.preview[454 * 453])
    }

    // 速さは待ち時間に効き、拡大しても同じ絵のフレームはまとめる
    @Test
    fun speedAndMergedFrames() {
        val a = IntArray(4) { 0x111111 }
        val b = IntArray(4) { 0x222222 }
        val art = renderWatchFace(gif(2, 2, a, a, b), Config(speed = 2.0))
        assertEquals(2, art.frameCount)
        assertEquals(listOf(100, 50), decodeGif(art.gif).delaysMillis)
    }

    @Test
    fun xmlPlacesTheGif() {
        val art = renderWatchFace(gif(64, 64, IntArray(64 * 64) { 0xF05050 }), Config())
        val xml = watchFaceXml(art)
        assertTrue(xml.contains("""<PartAnimatedImage name="gif" x="3" y="3" width="448" height="448">"""))
        assertTrue(xml.contains("""<Fill color="#fff05050"/>"""))
    }

    // APK の中身を中身で見分けて差し替え、署名と classes.dex を捨て、resources.arsc を縮めずに 4 バイト境界にそろえる
    @Test
    fun rewriteApkReplacesAndAligns() {
        val template = ByteArrayOutputStream().also { bytes ->
            ZipOutputStream(bytes).use { z ->
                for ((name, data) in listOf(
                    "AndroidManifest.xml" to ByteArray(13) { 1 },
                    "META-INF/CERT.SF" to ByteArray(3),
                    "classes.dex" to ByteArray(3),
                    "res/Af.xml" to byteArrayOf(3, 0, 8, 0),
                    "res/li.xml" to "<?xml old".toByteArray(),
                    "res/zr.gif" to "GIF".toByteArray(),
                    "res/fs.png" to "PNG".toByteArray(),
                    "resources.arsc" to ByteArray(101) { 7 },
                )) {
                    z.putNextEntry(ZipEntry(name))
                    z.write(data)
                    z.closeEntry()
                }
            }
        }.toByteArray()
        val out = ByteArrayOutputStream()
        rewriteWatchFaceApk(ByteArrayInputStream(template), out, "new".toByteArray(), "gif".toByteArray(), "png".toByteArray())

        val file = File.createTempFile("face", ".apk").apply { deleteOnExit() }
        file.writeBytes(out.toByteArray())
        ZipFile(file).use { z ->
            assertEquals(
                listOf("AndroidManifest.xml", "res/Af.xml", "res/li.xml", "res/zr.gif", "res/fs.png", "resources.arsc"),
                z.entries().toList().map { it.name },
            )
            fun text(name: String) = String(z.getInputStream(z.getEntry(name)).readBytes())
            assertEquals("new", text("res/li.xml"))
            assertEquals("gif", text("res/zr.gif"))
            assertEquals("png", text("res/fs.png"))
            assertArrayEquals(byteArrayOf(3, 0, 8, 0), z.getInputStream(z.getEntry("res/Af.xml")).readBytes())
            val arsc = z.getEntry("resources.arsc")
            assertEquals(ZipEntry.STORED, arsc.method)
            assertArrayEquals(ByteArray(101) { 7 }, z.getInputStream(arsc).readBytes())
        }
        // 中身の始まりを探す（ローカルの見出しの後ろ）
        val bytes = out.toByteArray()
        val name = "resources.arsc".toByteArray()
        val at = (0 until bytes.size - name.size).first { i ->
            bytes[i] == 0x50.toByte() && bytes[i + 1] == 0x4B.toByte() && bytes[i + 2] == 3.toByte() &&
                (0 until name.size).all { bytes[i + 30 + it] == name[it] }
        }
        val extraLength = (bytes[at + 28].toInt() and 0xFF) or ((bytes[at + 29].toInt() and 0xFF) shl 8)
        assertEquals(0, (at + 30 + name.size + extraLength) % 4)
    }
}
