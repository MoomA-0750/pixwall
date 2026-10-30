package xyz.mooma0750.pixwall

import android.app.WallpaperManager
import android.content.ComponentName
import android.content.Intent
import android.content.res.Configuration
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.asImageBitmap
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.Calendar
import xyz.mooma0750.pixwall.core.Config
import xyz.mooma0750.pixwall.core.SlotRef
import xyz.mooma0750.pixwall.core.SwitchBy
import xyz.mooma0750.pixwall.core.activeTimeIndex
import xyz.mooma0750.pixwall.core.allImages
import xyz.mooma0750.pixwall.core.mainSlot
import xyz.mooma0750.pixwall.core.slot
import xyz.mooma0750.pixwall.core.switchTo
import xyz.mooma0750.pixwall.core.withMain
import xyz.mooma0750.pixwall.core.withSlot
import xyz.mooma0750.pixwall.ui.ImageState
import xyz.mooma0750.pixwall.ui.PixwallTheme
import xyz.mooma0750.pixwall.ui.Preview
import xyz.mooma0750.pixwall.ui.SettingsScreen

/**
 * 設定画面。壁紙とは別のプロセス（:settings）で動き、閉じたら終わる（壁紙のプロセスに Compose を読み込まない）。
 * 変えるとすぐ config.json に書き、壁紙が気づいて描き直す。
 * 画像を切り替えるときは、選んだ1つ（editing）の画像と並べ方を、今までの欄（画像・並べ方・位置）で編集する
 */
class SettingsActivity : ComponentActivity() {
    private var config by mutableStateOf(Config())
    private var editing by mutableStateOf<SlotRef>(SlotRef.Main)
    private var preview by mutableStateOf<Preview?>(null)
    private var imageState by mutableStateOf(ImageState.NONE)
    private var isActiveWallpaper by mutableStateOf(false)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        config = ConfigStore.load(this)
        editing = initialEditing(config)

        setContent {
            val scope = rememberCoroutineScope()
            // 編集している画像が替わったら、見本を読み直す
            val editingImage = config.slot(editing).image
            LaunchedEffect(editingImage) {
                if (editingImage == null) {
                    preview = null
                    imageState = ImageState.NONE
                    return@LaunchedEffect
                }
                val loaded = withContext(Dispatchers.IO) { loadPreview(Uri.parse(editingImage)) }
                preview = loaded
                imageState = if (loaded != null) ImageState.LOADED else ImageState.UNREADABLE
            }
            val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
                if (uri == null) return@rememberLauncherForActivityResult
                scope.launch {
                    val loaded = withContext(Dispatchers.IO) { loadPreview(uri) }
                    // 読めない画像は設定に書かない
                    if (loaded == null) {
                        imageState = ImageState.UNREADABLE
                        return@launch
                    }
                    val next = config.withSlot(editing, config.slot(editing).copy(image = uri.toString()))
                    keepPermission(uri, next)
                    preview = loaded
                    imageState = ImageState.LOADED
                    change(next)
                }
            }
            PixwallTheme {
                // 今までの欄には、編集しているものを上の階層の画像と並べ方に入れて渡し、変えたら編集しているものへ戻す
                val view = config.withMain(config.slot(editing))
                SettingsScreen(
                    config = view,
                    editing = editing,
                    activeTime = config.activeTimeIndex(minuteOfDay()),
                    onEdit = { editing = it },
                    onSwitch = { mode ->
                        change(config.switchTo(mode))
                        editing = initialEditing(config)
                    },
                    onTimes = { times ->
                        change(config.copy(timeSlots = times))
                        if (editing is SlotRef.Time && (editing as SlotRef.Time).index > times.lastIndex) {
                            editing = SlotRef.Time(times.lastIndex)
                        }
                    },
                    preview = preview,
                    imageState = imageState,
                    isActiveWallpaper = isActiveWallpaper,
                    onPickImage = { picker.launch(arrayOf("image/*")) },
                    onSetWallpaper = ::openWallpaperPicker,
                    onChange = { next -> change(next.withMain(config.mainSlot).withSlot(editing, next.mainSlot)) },
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()
        isActiveWallpaper = WallpaperManager.getInstance(this).wallpaperInfo?.component == wallpaperComponent()
    }

    private fun change(next: Config) {
        if (next == config) return
        config = next
        ConfigStore.save(this, next)
    }

    private fun minuteOfDay(): Int {
        val now = Calendar.getInstance()
        return now.get(Calendar.HOUR_OF_DAY) * 60 + now.get(Calendar.MINUTE)
    }

    /** 開いたとき・切り替え方を変えたときは、今出しているものを編集する */
    private fun initialEditing(c: Config): SlotRef = when (c.switchBy) {
        SwitchBy.NONE -> SlotRef.Main
        SwitchBy.TIME -> c.activeTimeIndex(minuteOfDay())?.let { SlotRef.Time(it) } ?: SlotRef.Main
        SwitchBy.APPEARANCE ->
            if (resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK == Configuration.UI_MODE_NIGHT_YES) SlotRef.Dark else SlotRef.Light
    }

    private fun loadPreview(uri: Uri): Preview? =
        runCatching { Frames.load(this, uri) }.getOrNull()?.let { frames ->
            frames.colorSample.recycle()
            Preview(frames.bitmaps.map { it.asImageBitmap() }, frames.delaysMillis, frames.autoBackground)
        }

    /**
     * 選んだ画像はコピーせず、場所（URI）だけ覚える。端末を再起動しても読めるように読む権限を持ち続け、
     * 前の画像の権限は、ほかの時刻・外観でも使っていなければ手放す
     */
    private fun keepPermission(uri: Uri, next: Config) {
        runCatching { contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
        val old = config.slot(editing).image ?: return
        if (old !in next.allImages) {
            runCatching { contentResolver.releasePersistableUriPermission(Uri.parse(old), Intent.FLAG_GRANT_READ_URI_PERMISSION) }
        }
    }

    /** OS のライブ壁紙のプレビューを開く。「壁紙に設定」を押すと、ホーム画面か、ホーム画面とロック画面に入る */
    private fun openWallpaperPicker() {
        startActivity(
            Intent(WallpaperManager.ACTION_CHANGE_LIVE_WALLPAPER)
                .putExtra(WallpaperManager.EXTRA_LIVE_WALLPAPER_COMPONENT, wallpaperComponent()),
        )
    }

    private fun wallpaperComponent() = ComponentName(this, PixwallWallpaperService::class.java)
}
