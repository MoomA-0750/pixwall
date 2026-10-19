package xyz.mooma0750.pixwall

import android.content.Context
import xyz.mooma0750.pixwall.core.Config
import java.io.File

/**
 * `config.json` をアプリの領域（filesDir）に置く。設定画面（:settings のプロセス）が書き、壁紙（主のプロセス）が FileObserver で気づく
 */
object ConfigStore {
    const val FILE_NAME = "config.json"

    fun file(context: Context) = File(context.filesDir, FILE_NAME)

    fun load(context: Context): Config = Config.parse(runCatching { file(context).readText() }.getOrNull())

    /** 別のファイルに書いてから入れ替える（読んでいる途中の壁紙が、書きかけを読まないように） */
    fun save(context: Context, config: Config) {
        val tmp = File(context.filesDir, "$FILE_NAME.tmp")
        tmp.writeText(config.toJson())
        tmp.renameTo(file(context))
    }
}
