package xyz.mooma0750.pixwall.wear

import android.content.Context
import xyz.mooma0750.pixwall.core.Config
import java.io.File

/** 文字盤に使える GIF。id は設定の image に書く（"asset:samples/…" か "file:gifs/…"） */
data class GifSource(val id: String, val name: String)

/**
 * 時計の中にある GIF。アプリに入れたサンプルと、アプリの領域の gifs/ に置いたもの
 * （スマホの pixwall から送ったもの。GifReceiverService が置く。試すときは adb で置いてもよい。docs/android-trial.md）。
 * gifs/ のものは新しい順に、サンプルより前に並べる
 */
class Library(private val context: Context) {
    private val dir = File(context.filesDir, "gifs")

    fun list(): List<GifSource> {
        val samples = context.assets.list("samples").orEmpty().sorted().map { GifSource("asset:samples/$it", it.removeSuffix(".gif")) }
        val files = dir.listFiles { f -> f.name.endsWith(".gif", ignoreCase = true) }.orEmpty()
            .sortedWith(compareByDescending<File> { it.lastModified() }.thenBy { it.name })
            .map { GifSource("file:gifs/${it.name}", it.name.removeSuffix(".gif")) }
        return files + samples
    }

    /** 受け取った GIF を置き、その id を返す。同じ名前なら入れ替える。書きかけを読まないよう、別のファイルに書いてから入れ替える */
    fun add(name: String, bytes: ByteArray): String {
        dir.mkdirs()
        val safe = name.substringAfterLast('/').removeSuffix(".gif").replace(Regex("[\\\\:*?\"<>|\\x00-\\x1f]"), "_").trim().ifEmpty { "image" } + ".gif"
        val temp = File(dir, ".$safe.tmp")
        temp.writeBytes(bytes)
        temp.renameTo(File(dir, safe))
        return "file:gifs/$safe"
    }

    fun read(id: String): ByteArray = when {
        id.startsWith("asset:") -> context.assets.open(id.removePrefix("asset:")).use { it.readBytes() }
        id.startsWith("file:") -> File(context.filesDir, id.removePrefix("file:")).readBytes()
        else -> error("unknown image $id")
    }
}

/** 設定は config.json（形はスマホ・Mac・Windows と同じ。image は上の id） */
class WearConfigStore(context: Context) {
    private val file = File(context.filesDir, "config.json")

    fun load(): Config = Config.parse(if (file.exists()) file.readText() else null)

    fun save(config: Config) {
        val temp = File(file.parentFile, "config.json.tmp")
        temp.writeText(config.toJson())
        temp.renameTo(file)
    }
}
