package xyz.mooma0750.pixwall

import android.content.Context
import android.net.Uri
import java.io.File
import java.security.MessageDigest

/**
 * 選んだ画像の写し。アプリの領域（filesDir/images/）に、画像の場所（config.json の image）から決めた名前で置く。
 * 壁紙と設定画面は写しを読むので、元の画像が消されても、写真の置き場の用意が起動の直後に遅れても、出し続けられる。
 * 写しが無い（前の版で選んだ）ときは元を読み、読めたらその場で写す
 */
object ImageCopies {
    private const val DIR = "images"

    fun file(context: Context, image: String): File = File(File(context.filesDir, DIR), name(image))

    /** 写しを読む。無ければ元を読んで写す。どちらも読めなければ例外 */
    fun read(context: Context, image: String): ByteArray {
        val copy = file(context, image)
        runCatching { copy.readBytes() }.getOrNull()?.let { return it }
        val bytes = readSource(context, image)
        runCatching { write(context, image, bytes) }
        return bytes
    }

    /** 元の画像を読む。消されたとき・読む権限が無いときは例外 */
    fun readSource(context: Context, image: String): ByteArray =
        context.contentResolver.openInputStream(Uri.parse(image))?.use { it.readBytes() } ?: error("cannot open $image")

    /** 元の画像がまだ読めるか（中身は読まない） */
    fun sourceExists(context: Context, image: String): Boolean =
        runCatching { context.contentResolver.openAssetFileDescriptor(Uri.parse(image), "r")?.use { true } ?: false }
            .getOrDefault(false)

    /** 別のファイルに書いてから入れ替える（読んでいる途中の壁紙が、書きかけを読まないように） */
    fun write(context: Context, image: String, bytes: ByteArray) {
        val target = file(context, image)
        target.parentFile?.mkdirs()
        val tmp = File(target.parentFile, "${target.name}.tmp")
        tmp.writeBytes(bytes)
        if (!tmp.renameTo(target)) {
            tmp.delete()
            error("cannot write ${target.name}")
        }
    }

    /** keep に無い画像の写しを消す（選び直した・時刻の1つを消したなど） */
    fun prune(context: Context, keep: Collection<String>) {
        val names = keep.map(::name).toSet()
        File(context.filesDir, DIR).listFiles()?.forEach { if (it.name !in names) it.delete() }
    }

    private fun name(image: String): String =
        MessageDigest.getInstance("SHA-256").digest(image.toByteArray()).joinToString("") { "%02x".format(it) }.take(32)
}
