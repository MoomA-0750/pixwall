package xyz.mooma0750.pixwall

import android.app.WallpaperManager
import android.content.ComponentName
import android.content.Intent
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
import xyz.mooma0750.pixwall.core.Config
import xyz.mooma0750.pixwall.ui.ImageState
import xyz.mooma0750.pixwall.ui.PixwallTheme
import xyz.mooma0750.pixwall.ui.Preview
import xyz.mooma0750.pixwall.ui.SettingsScreen

/**
 * 設定画面。壁紙とは別のプロセス（:settings）で動き、閉じたら終わる（壁紙のプロセスに Compose を読み込まない）。
 * 変えるとすぐ config.json に書き、壁紙が気づいて描き直す
 */
class SettingsActivity : ComponentActivity() {
    private var config by mutableStateOf(Config())
    private var preview by mutableStateOf<Preview?>(null)
    private var imageState by mutableStateOf(ImageState.NONE)
    private var isActiveWallpaper by mutableStateOf(false)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        config = ConfigStore.load(this)

        setContent {
            val scope = rememberCoroutineScope()
            LaunchedEffect(Unit) {
                val image = config.image ?: return@LaunchedEffect
                val loaded = withContext(Dispatchers.IO) { loadPreview(Uri.parse(image)) }
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
                    keepPermission(uri)
                    preview = loaded
                    imageState = ImageState.LOADED
                    change(config.copy(image = uri.toString()))
                }
            }
            PixwallTheme {
                SettingsScreen(
                    config = config,
                    preview = preview,
                    imageState = imageState,
                    isActiveWallpaper = isActiveWallpaper,
                    onPickImage = { picker.launch(arrayOf("image/*")) },
                    onSetWallpaper = ::openWallpaperPicker,
                    onChange = ::change,
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

    private fun loadPreview(uri: Uri): Preview? =
        runCatching { Frames.load(this, uri) }.getOrNull()?.let { frames ->
            frames.colorSample.recycle()
            Preview(frames.bitmaps.map { it.asImageBitmap() }, frames.delaysMillis, frames.autoBackground)
        }

    /** 選んだ画像はコピーせず、場所（URI）だけ覚える。端末を再起動しても読めるように読む権限を持ち続け、前の画像の権限は手放す */
    private fun keepPermission(uri: Uri) {
        runCatching { contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
        val old = config.image?.let(Uri::parse) ?: return
        if (old != uri) runCatching { contentResolver.releasePersistableUriPermission(old, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
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
