package xyz.mooma0750.pixwall

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import android.util.Log
import com.google.android.gms.tasks.Tasks
import com.google.android.gms.wearable.Asset
import com.google.android.gms.wearable.CapabilityClient
import com.google.android.gms.wearable.PutDataMapRequest
import com.google.android.gms.wearable.Wearable
import xyz.mooma0750.pixwall.core.isGif

/** 送った結果 */
enum class SendResult { SENT, NO_WATCH, NOT_GIF, FAILED }

/**
 * 選んだ GIF を時計の pixwall へ送る（Data Layer。Google Play 開発者サービスが運ぶ）。
 * 時計は pixwall のアプリが入っていると [WATCH_CAPABILITY] を名乗るので、それでつながっているかを見る。
 * 設定画面のプロセスだけで使う（壁紙のプロセスには読み込まない）。どれも待つので、主のスレッドでは呼ばない
 */
object WatchSender {
    /** 時計のアプリの res/values/wear.xml と同じ名前 */
    const val WATCH_CAPABILITY = "pixwall_watch"

    /** 時計の受け手と同じ決まり。/gif/<ファイルの名前> に、GIF を asset "gif" で置く */
    const val PATH_PREFIX = "/gif/"
    const val KEY_GIF = "gif"
    const val KEY_SENT_AT = "sentAt"

    /** pixwall の入った時計が今つながっているか */
    fun hasWatch(context: Context): Boolean =
        runCatching {
            Tasks.await(Wearable.getCapabilityClient(context).getCapability(WATCH_CAPABILITY, CapabilityClient.FILTER_REACHABLE))
                .nodes.isNotEmpty()
        }.onFailure { Log.i(TAG, "no Data Layer: ${it.message}") }.getOrDefault(false)

    fun send(context: Context, uri: Uri): SendResult {
        if (!hasWatch(context)) return SendResult.NO_WATCH
        return runCatching {
            val bytes = context.contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: error("cannot open $uri")
            if (!isGif(bytes)) return SendResult.NOT_GIF
            val request = PutDataMapRequest.create(PATH_PREFIX + fileName(context, uri)).apply {
                dataMap.putAsset(KEY_GIF, Asset.createFromBytes(bytes))
                // 同じ GIF を送り直しても、時計に届くように（中身が同じだと Data Layer は知らせない）
                dataMap.putLong(KEY_SENT_AT, System.currentTimeMillis())
            }.asPutDataRequest().setUrgent()
            Tasks.await(Wearable.getDataClient(context).putDataItem(request))
            SendResult.SENT
        }.onFailure { Log.w(TAG, "cannot send $uri", it) }.getOrDefault(SendResult.FAILED)
    }

    /** 時計の一覧に出す名前。表示名を使い、ファイルの名前に使えない字は置き換える */
    private fun fileName(context: Context, uri: Uri): String {
        val display = runCatching {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) c.getString(0) else null
            }
        }.getOrNull() ?: uri.lastPathSegment ?: "image"
        val base = display.substringBeforeLast('.').replace(Regex("[/\\\\:*?\"<>|\\x00-\\x1f]"), "_").trim().ifEmpty { "image" }
        return "$base.gif"
    }

    private const val TAG = "pixwall"
}
