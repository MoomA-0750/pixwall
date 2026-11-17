package xyz.mooma0750.pixwall.wear

import android.util.Log
import com.google.android.gms.tasks.Tasks
import com.google.android.gms.wearable.DataEvent
import com.google.android.gms.wearable.DataEventBuffer
import com.google.android.gms.wearable.DataMapItem
import com.google.android.gms.wearable.Wearable
import com.google.android.gms.wearable.WearableListenerService
import xyz.mooma0750.pixwall.core.isGif

/**
 * スマホの pixwall が送った GIF を受け取り、アプリの領域の gifs/ に置く（一覧に出て、時計のアプリが開いていればそれを選ぶ）。
 * 置いたら Data Layer からは消す（スマホと時計の両方に GIF を持ち続けないように）。
 * 決まりはスマホの WatchSender と同じ: /gif/<ファイルの名前> に、GIF を asset "gif" で置く
 */
class GifReceiverService : WearableListenerService() {
    override fun onDataChanged(events: DataEventBuffer) {
        val client = Wearable.getDataClient(this)
        for (event in events) {
            if (event.type != DataEvent.TYPE_CHANGED) continue
            val item = event.dataItem
            val path = item.uri.path ?: continue
            if (!path.startsWith(PATH_PREFIX)) continue
            val name = path.removePrefix(PATH_PREFIX)
            runCatching {
                val asset = DataMapItem.fromDataItem(item).dataMap.getAsset(KEY_GIF) ?: error("no asset")
                val bytes = Tasks.await(client.getFdForAsset(asset)).inputStream.use { it.readBytes() }
                if (!isGif(bytes)) error("not a GIF")
                val id = Library(this).add(name, bytes)
                // 送ってきたものを選んでおく（時計で開いたら「文字盤にする」を押すだけ）
                val store = WearConfigStore(this)
                store.save(store.load().copy(image = id))
                Log.i(TAG, "received $name (${bytes.size} bytes)")
            }.onFailure { Log.w(TAG, "cannot receive $path", it) }
            runCatching { Tasks.await(client.deleteDataItems(item.uri)) }
        }
    }

    companion object {
        private const val TAG = "pixwall"
        const val PATH_PREFIX = "/gif/"
        const val KEY_GIF = "gif"
    }
}
