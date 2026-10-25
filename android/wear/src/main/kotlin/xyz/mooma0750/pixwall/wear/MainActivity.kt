package xyz.mooma0750.pixwall.wear

import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.wear.compose.foundation.lazy.TransformingLazyColumn
import androidx.wear.compose.foundation.lazy.rememberTransformingLazyColumnState
import androidx.wear.compose.material3.AlertDialog
import androidx.wear.compose.material3.AppScaffold
import androidx.wear.compose.material3.ConfirmationDialog
import androidx.wear.compose.material3.EdgeButton
import androidx.wear.compose.material3.EdgeButtonSize
import androidx.wear.compose.material3.Icon
import androidx.wear.compose.material3.ListHeader
import androidx.wear.compose.material3.MaterialTheme
import androidx.wear.compose.material3.MotionScheme
import androidx.wear.compose.material3.RadioButton
import androidx.wear.compose.material3.ScreenScaffold
import androidx.wear.compose.material3.Slider
import androidx.wear.compose.material3.SuccessConfirmationDialog
import androidx.wear.compose.material3.SurfaceTransformation
import androidx.wear.compose.material3.Text
import androidx.wear.compose.material3.TimeText
import androidx.wear.compose.material3.confirmationDialogCurvedText
import androidx.wear.compose.material3.ConfirmationDialogDefaults
import androidx.wear.compose.material3.dynamicColorScheme
import androidx.wear.compose.material3.lazy.rememberTransformationSpec
import androidx.wear.compose.material3.lazy.transformedHeight
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import xyz.mooma0750.pixwall.core.Config
import xyz.mooma0750.pixwall.core.Fit
import xyz.mooma0750.pixwall.core.GifImage
import xyz.mooma0750.pixwall.core.decodeGif
import xyz.mooma0750.pixwall.core.previewWatchFace
import kotlin.math.roundToInt

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme(
                colorScheme = dynamicColorScheme(this) ?: MaterialTheme.colorScheme,
                motionScheme = MotionScheme.expressive(),
            ) {
                // 試すとき: デバッグ版は `am start … --ez install true` で開くと、読み込み次第「文字盤にする」を押したのと同じにする
                val autoInstall = applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0 && intent.getBooleanExtra("install", false)
                AppScaffold(timeText = { TimeText() }) { SettingsScreen(autoInstall) }
            }
        }
    }
}

@Composable
private fun SettingsScreen(autoInstall: Boolean) {
    val context = LocalContext.current
    val library = remember { Library(context) }
    val store = remember { WearConfigStore(context) }
    val maker = remember { WatchFaceMaker(context) }
    val scope = rememberCoroutineScope()
    val images = remember { library.list() }

    var config by remember {
        mutableStateOf(store.load().let { c -> if (images.any { it.id == c.image }) c else c.copy(image = images.firstOrNull()?.id) })
    }
    fun update(c: Config) {
        config = c
        store.save(c)
    }

    val gif by produceState<GifImage?>(null, config.image) {
        value = config.image?.let { id -> withContext(Dispatchers.IO) { runCatching { decodeGif(library.read(id)) }.getOrNull() } }
    }
    val preview by produceState<ImageBitmap?>(null, gif, config) {
        val g = gif ?: return@produceState
        value = withContext(Dispatchers.Default) {
            val size = 454
            Bitmap.createBitmap(previewWatchFace(g, config, size), size, size, Bitmap.Config.ARGB_8888).asImageBitmap()
        }
    }

    var installing by remember { mutableStateOf(false) }
    var result by remember { mutableStateOf<InstallResult?>(null) }
    fun install() {
        val g = gif ?: return
        installing = true
        scope.launch {
            result = runCatching { maker.install(g, config) }
                .getOrElse { InstallResult.Failed(it.message ?: it.javaClass.simpleName) }
            Log.i("pixwall", "watch face: $result")
            installing = false
        }
    }
    // 今の文字盤にしてよいかは一度だけ聞ける。聞いたあとは、許されても断られても入れる
    val askActive = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { install() }
    val prefs = remember { context.getSharedPreferences("wear", 0) }

    LaunchedEffect(gif) { if (autoInstall && gif != null) install() }

    val state = rememberTransformingLazyColumnState()
    val spec = rememberTransformationSpec()
    ScreenScaffold(
        scrollState = state,
        edgeButton = {
            EdgeButton(
                onClick = {
                    val granted = context.checkSelfPermission(WatchFaceMaker.SET_ACTIVE_PERMISSION) == PackageManager.PERMISSION_GRANTED
                    if (!granted && !prefs.getBoolean("askedActive", false)) {
                        prefs.edit().putBoolean("askedActive", true).apply()
                        askActive.launch(WatchFaceMaker.SET_ACTIVE_PERMISSION)
                    } else {
                        install()
                    }
                },
                enabled = gif != null && !installing,
                buttonSize = EdgeButtonSize.Medium,
            ) {
                Text(stringResource(if (installing) R.string.installing else R.string.install))
            }
        },
    ) { contentPadding ->
        TransformingLazyColumn(state = state, contentPadding = contentPadding) {
            item {
                ListHeader(modifier = Modifier.transformedHeight(this, spec), transformation = SurfaceTransformation(spec)) {
                    Text(stringResource(R.string.app_name))
                }
            }
            item {
                // 読み込む前から高さを取っておく（あとから伸びると、リストがずれて見出しが隠れる）
                Box(Modifier.fillMaxWidth().height(120.dp).transformedHeight(this, spec), contentAlignment = Alignment.Center) {
                    preview?.let {
                        Image(
                            bitmap = it,
                            contentDescription = stringResource(R.string.preview),
                            modifier = Modifier.size(120.dp).clip(CircleShape),
                            // ドットをぼかさない
                            filterQuality = FilterQuality.None,
                        )
                    }
                }
            }
            item {
                ListHeader(modifier = Modifier.transformedHeight(this, spec), transformation = SurfaceTransformation(spec)) {
                    Text(stringResource(R.string.image))
                }
            }
            images.forEach { source ->
                item(key = source.id) {
                    RadioButton(
                        selected = config.image == source.id,
                        onSelect = { update(config.copy(image = source.id)) },
                        label = { Text(source.name, maxLines = 2) },
                        modifier = Modifier.fillMaxWidth().transformedHeight(this, spec),
                        transformation = SurfaceTransformation(spec),
                    )
                }
            }
            item {
                ListHeader(modifier = Modifier.transformedHeight(this, spec), transformation = SurfaceTransformation(spec)) {
                    Text(stringResource(R.string.fit))
                }
            }
            listOf(
                Triple(Fit.INTEGER, R.string.fit_integer, R.string.fit_integer_detail),
                Triple(Fit.FIT, R.string.fit_fit, R.string.fit_fit_detail),
                Triple(Fit.FILL, R.string.fit_fill, R.string.fit_fill_detail),
                Triple(Fit.FREE, R.string.fit_free, R.string.fit_free_detail),
            ).forEach { (fit, label, detail) ->
                item(key = fit.json) {
                    RadioButton(
                        selected = config.fit == fit,
                        onSelect = { update(config.copy(fit = fit)) },
                        label = { Text(stringResource(label), maxLines = 2) },
                        secondaryLabel = { Text(stringResource(detail), maxLines = 3) },
                        modifier = Modifier.fillMaxWidth().transformedHeight(this, spec),
                        transformation = SurfaceTransformation(spec),
                    )
                }
            }
            // 位置は「画面に合わせる」「画面を埋める」のとき、大きさと横・縦の位置は「自由に置く」のとき（整数倍は中央のまま）。
            // どれも 10 刻み（時計の画面では細かく合わせにくい）
            val sliders = when (config.fit) {
                Fit.FIT, Fit.FILL -> listOf(
                    TenthSlider(R.string.position_value, config.position, 0..10) { config.copy(position = it) },
                )
                Fit.FREE -> listOf(
                    TenthSlider(R.string.free_size_value, config.freeSize, 1..30) { config.copy(freeSize = it) },
                    TenthSlider(R.string.free_x_value, config.freeX, 0..10) { config.copy(freeX = it) },
                    TenthSlider(R.string.free_y_value, config.freeY, 0..10) { config.copy(freeY = it) },
                )
                else -> emptyList()
            }
            sliders.forEach { s ->
                item(key = "${s.label}-label") {
                    ListHeader(modifier = Modifier.transformedHeight(this, spec), transformation = SurfaceTransformation(spec)) {
                        Text(stringResource(s.label, (s.value * 100).roundToInt()))
                    }
                }
                item(key = "${s.label}-slider") {
                    Slider(
                        value = (s.value * 10).roundToInt().coerceIn(s.range.first, s.range.last),
                        onValueChange = { update(s.change(it / 10.0)) },
                        valueProgression = s.range,
                        segmented = false,
                        modifier = Modifier.transformedHeight(this, spec),
                    )
                }
            }
        }
    }

    ResultDialogs(result) { result = null }
}

/** 10 分の 1 刻みのつまみ。label は % の数を %1$d で受け、range は値の 10 倍 */
private class TenthSlider(val label: Int, val value: Double, val range: IntRange, val change: (Double) -> Config)

@Composable
private fun ResultDialogs(result: InstallResult?, dismiss: () -> Unit) {
    val curvedStyle = ConfirmationDialogDefaults.curvedTextStyle
    val done = stringResource(R.string.installed_active)
    SuccessConfirmationDialog(
        visible = result == InstallResult.Active,
        onDismissRequest = dismiss,
        curvedText = { confirmationDialogCurvedText(done, curvedStyle) },
    )
    ConfirmationDialog(
        visible = result == InstallResult.Installed,
        onDismissRequest = dismiss,
        text = { Text(stringResource(R.string.installed_choose)) },
    ) {
        Icon(
            painter = painterResource(R.drawable.ic_watch),
            contentDescription = null,
            modifier = Modifier.size(ConfirmationDialogDefaults.SmallIconSize),
        )
    }
    AlertDialog(
        visible = result is InstallResult.TooLarge || result is InstallResult.Failed,
        onDismissRequest = dismiss,
        title = { Text(stringResource(R.string.install_failed)) },
        text = {
            when (result) {
                is InstallResult.TooLarge -> Text(stringResource(R.string.too_large, result.megabytes))
                is InstallResult.Failed -> Text(result.message, maxLines = 6)
                else -> {}
            }
        },
    )
}
