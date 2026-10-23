package xyz.mooma0750.pixwall.core

import java.io.FilterOutputStream
import java.io.InputStream
import java.io.OutputStream
import java.util.zip.CRC32
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/**
 * 土台の APK の中身を、replace が返したものに差し替えて書く（null ならそのまま）。署名はまだ付けない。
 * 残すのは AndroidManifest.xml・resources.arsc・res/ だけ（Watch Face Push の決まり。古い署名や classes.dex は捨てる）。
 * resources.arsc は縮めずに 4 バイト境界にそろえる（Android 11 から、そうでない APK は入らない）
 */
fun rewriteApk(template: InputStream, out: OutputStream, replace: (name: String, data: ByteArray) -> ByteArray?) {
    val counting = CountingOutputStream(out)
    val zip = ZipOutputStream(counting)
    ZipInputStream(template).use { input ->
        while (true) {
            val entry = input.nextEntry ?: break
            val name = entry.name
            if (entry.isDirectory || !(name == "AndroidManifest.xml" || name == "resources.arsc" || name.startsWith("res/"))) continue
            val original = input.readBytes()
            write(zip, counting, name, replace(name, original) ?: original, stored = name == "resources.arsc")
        }
    }
    zip.finish()
}

/**
 * 文字盤の土台（android/wearface）の中身を差し替える。release のビルドはファイルの名前を縮める（res/zr.gif など）ので、
 * 名前ではなく中身で見分ける: GIF は1つだけの .gif、見本は1つだけの .png、文字盤の XML は res/ の中で唯一コンパイルされていない XML
 */
fun rewriteWatchFaceApk(template: InputStream, out: OutputStream, xml: ByteArray, gif: ByteArray, preview: ByteArray) {
    val replaced = mutableListOf<String>()
    rewriteApk(template, out) { name, data ->
        when {
            name.endsWith(".gif") -> gif.also { replaced += "gif" }
            name.endsWith(".png") -> preview.also { replaced += "png" }
            name.startsWith("res/") && name.endsWith(".xml") && String(data, 0, minOf(5, data.size), Charsets.US_ASCII) == "<?xml" ->
                xml.also { replaced += "xml" }
            else -> null
        }
    }
    require(replaced.sorted() == listOf("gif", "png", "xml")) { "unexpected template: $replaced" }
}

private fun write(zip: ZipOutputStream, counting: CountingOutputStream, name: String, data: ByteArray, stored: Boolean) {
    val entry = ZipEntry(name)
    // DOS の日時で書ける日にしておく（1980 年より前だと、拡張の日時の欄が足されて詰め方がずれる）
    entry.time = FIXED_TIME
    if (stored) {
        val crc = CRC32()
        crc.update(data)
        entry.method = ZipEntry.STORED
        entry.size = data.size.toLong()
        entry.compressedSize = data.size.toLong()
        entry.crc = crc.value
        // 中身の始まりが 4 の倍数になるよう、余分の欄（zipalign と同じ 0xD935）で詰める
        val nameLength = name.toByteArray(Charsets.UTF_8).size
        val start = counting.count + 30 + nameLength + 6
        val pad = ((4 - start % 4) % 4).toInt()
        val extra = ByteArray(6 + pad)
        extra[0] = 0x35
        extra[1] = 0xD9.toByte()
        extra[2] = (2 + pad).toByte()
        extra[4] = 4
        entry.extra = extra
    } else {
        entry.method = ZipEntry.DEFLATED
    }
    zip.putNextEntry(entry)
    zip.write(data)
    zip.closeEntry()
}

private val FIXED_TIME = java.util.GregorianCalendar(1981, 0, 1).timeInMillis

private class CountingOutputStream(out: OutputStream) : FilterOutputStream(out) {
    var count = 0L

    override fun write(b: Int) {
        out.write(b)
        count++
    }

    override fun write(b: ByteArray, off: Int, len: Int) {
        out.write(b, off, len)
        count += len
    }
}
