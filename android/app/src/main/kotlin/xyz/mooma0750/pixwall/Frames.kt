package xyz.mooma0750.pixwall

import android.content.Context
import android.graphics.Bitmap
import android.graphics.ImageDecoder
import android.net.Uri
import xyz.mooma0750.pixwall.core.decodeGif
import xyz.mooma0750.pixwall.core.isGif
import xyz.mooma0750.pixwall.core.mergingRepeats

/**
 * 元の大きさのまま全部読んだフレーム。
 * autoBackground は「自動」の余白の色（1枚目の左上の1ドット、0xRRGGBB）、colorSample は端末の色（Material You）を決めるための小さな1枚目
 */
class Frames(
    val width: Int,
    val height: Int,
    val bitmaps: List<Bitmap>,
    val delaysMillis: List<Int>,
    val autoBackground: Int,
    val colorSample: Bitmap,
) {
    val count get() = bitmaps.size

    fun recycle() {
        bitmaps.forEach(Bitmap::recycle)
        colorSample.recycle()
    }

    companion object {
        /**
         * 画像を読む。GIF は自分で全フレーム読み、そのほか（PNG・WebP・JPEG など）は1枚目だけを静止画として読む。読めなければ例外
         */
        fun load(context: Context, uri: Uri): Frames {
            val bytes = context.contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: error("cannot open $uri")
            val software: List<Bitmap>
            val delays: List<Int>
            if (isGif(bytes)) {
                val gif = decodeGif(bytes).mergingRepeats()
                software = gif.frames.map { Bitmap.createBitmap(it, gif.width, gif.height, Bitmap.Config.ARGB_8888) }
                delays = gif.delaysMillis
            } else {
                val bitmap = ImageDecoder.decodeBitmap(ImageDecoder.createSource(bytes)) { decoder, _, _ ->
                    decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
                }
                software = listOf(bitmap)
                delays = listOf(0)
            }
            val first = software[0]
            val autoBackground = first.getPixel(0, 0) and 0xFFFFFF
            val sampleScale = minOf(1.0, 64.0 / maxOf(first.width, first.height))
            // 縮めないときの createScaledBitmap は同じ Bitmap を返すので、必ず別の Bitmap にする（見本だけ手放せるように）
            val colorSample =
                if (sampleScale >= 1.0) {
                    first.copy(Bitmap.Config.ARGB_8888, false)
                } else {
                    Bitmap.createScaledBitmap(
                        first,
                        maxOf(1, (first.width * sampleScale).toInt()),
                        maxOf(1, (first.height * sampleScale).toInt()),
                        false,
                    )
                }
            return Frames(first.width, first.height, software, delays, autoBackground, colorSample)
        }
    }
}
