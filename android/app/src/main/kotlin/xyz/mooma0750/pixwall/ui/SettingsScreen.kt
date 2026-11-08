package xyz.mooma0750.pixwall.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ButtonGroupDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.SegmentedListItem
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TimePicker
import androidx.compose.material3.TimePickerDialog
import androidx.compose.material3.ToggleButton
import androidx.compose.material3.rememberTimePickerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import xyz.mooma0750.pixwall.R
import xyz.mooma0750.pixwall.core.Config
import xyz.mooma0750.pixwall.core.Fit
import xyz.mooma0750.pixwall.core.SPEEDS
import xyz.mooma0750.pixwall.core.SlotRef
import xyz.mooma0750.pixwall.core.SwitchBy
import xyz.mooma0750.pixwall.core.TimeSlot
import xyz.mooma0750.pixwall.core.formatClock
import xyz.mooma0750.pixwall.core.mainSlot
import xyz.mooma0750.pixwall.core.frameDelayMillis
import xyz.mooma0750.pixwall.core.layout
import kotlin.math.roundToInt

/** 設定画面で動かす画像。全フレーム（続く同じ絵はまとめたあと）と待ち時間、「自動」の余白の色 */
class Preview(val frames: List<ImageBitmap>, val delaysMillis: List<Int>, val autoBackground: Int)

/** 画像の状態 */
enum class ImageState { NONE, LOADED, UNREADABLE }

/**
 * 設定画面。壁紙をつけたときの画面の見本を動かしながら、設定を変えられる（スマホは見本が上、広い画面は左）。
 * 見本を押すと、壁紙と同じ大きさ（画面のピクセルそのまま）で全画面に出る
 */
@Composable
fun SettingsScreen(
    config: Config,
    editing: SlotRef,
    activeTime: Int?,
    onEdit: (SlotRef) -> Unit,
    onSwitch: (SwitchBy) -> Unit,
    onTimes: (List<TimeSlot>) -> Unit,
    preview: Preview?,
    imageState: ImageState,
    isActiveWallpaper: Boolean,
    onPickImage: () -> Unit,
    onSetWallpaper: () -> Unit,
    onChange: (Config) -> Unit,
) {
    val index = rememberFrameIndex(preview, config.speed)
    var fullScreen by rememberSaveable { mutableStateOf(false) }
    if (fullScreen && preview != null) {
        BackHandler { fullScreen = false }
        val close = stringResource(R.string.preview_close)
        WallpaperCanvas(config, preview, index, screen = null, Modifier.fillMaxSize().clickable(onClickLabel = close) { fullScreen = false })
        return
    }
    // 窓は画面いっぱい（edge-to-edge）なので、窓の大きさが壁紙の面の大きさになる
    val screen = LocalWindowInfo.current.containerSize
    val openLabel = stringResource(R.string.preview_open)
    val openFullScreen = { if (preview != null) fullScreen = true }
    Surface(color = MaterialTheme.colorScheme.surface) {
        BoxWithConstraints(Modifier.fillMaxSize()) {
            val previewHeight = maxHeight * PREVIEW_HEIGHT
            val list = @Composable { modifier: Modifier ->
                SettingsList(
                    config, editing, activeTime, onEdit, onSwitch, onTimes,
                    preview, imageState, isActiveWallpaper, onPickImage, onSetWallpaper, onChange, modifier,
                )
            }
            if (maxWidth >= WIDE_LAYOUT) {
                Row(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal))) {
                    ScreenPreview(config, preview, index, screen, openLabel, openFullScreen, Modifier.weight(1f).fillMaxHeight().padding(24.dp))
                    list(Modifier.width(SETTINGS_WIDTH).fillMaxHeight())
                }
            } else {
                Column(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal))) {
                    ScreenPreview(config, preview, index, screen, openLabel, openFullScreen, Modifier.fillMaxWidth().height(previewHeight).padding(16.dp))
                    list(Modifier.weight(1f))
                }
            }
        }
    }
}

// スマホを横にしたときや折りたたみを開いたときは、見本と設定を横に並べる
private val WIDE_LAYOUT = 640.dp
private val SETTINGS_WIDTH = 440.dp
// スマホでは、画面の高さのこれだけを見本にする（残りで設定をスクロールする）
private const val PREVIEW_HEIGHT = 0.42f

@Composable
private fun SettingsList(
    config: Config,
    editing: SlotRef,
    activeTime: Int?,
    onEdit: (SlotRef) -> Unit,
    onSwitch: (SwitchBy) -> Unit,
    onTimes: (List<TimeSlot>) -> Unit,
    preview: Preview?,
    imageState: ImageState,
    isActiveWallpaper: Boolean,
    onPickImage: () -> Unit,
    onSetWallpaper: () -> Unit,
    onChange: (Config) -> Unit,
    modifier: Modifier,
) {
    val navigationBar = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    LazyColumn(
        modifier = modifier,
        contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 8.dp, bottom = navigationBar + 24.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        item {
            ImageCard(preview != null, imageState, isActiveWallpaper, onPickImage, onSetWallpaper)
        }
        switchSection(config, editing, activeTime, onEdit, onSwitch, onTimes)
        fitSection(config, onChange)
        if (config.fit == Fit.FIT || config.fit == Fit.FILL) {
            item {
                Section(stringResource(R.string.position))
                PositionSlider(config, onChange)
            }
        }
        if (config.fit == Fit.FREE) {
            item {
                Section(stringResource(R.string.free_section))
                FreeSliders(config, onChange)
            }
        }
        backgroundSection(config, preview, onChange)
        item {
            Section(stringResource(R.string.speed))
            SpeedButtons(config, onChange)
        }
        item {
            Section(stringResource(R.string.pause_section))
            SegmentedListItem(
                checked = config.pauseOnLowPower,
                onCheckedChange = { onChange(config.copy(pauseOnLowPower = it)) },
                shapes = ListItemDefaults.segmentedShapes(0, 1),
                supportingContent = { Text(stringResource(R.string.pause_on_low_power_detail)) },
                trailingContent = { Switch(checked = config.pauseOnLowPower, onCheckedChange = null) },
            ) {
                Text(stringResource(R.string.pause_on_low_power))
            }
        }
    }
}

/**
 * 出す画像を何で切り替えるか。時刻のときは時刻の一覧（行を選ぶと、下の欄でその時刻の画像と並べ方を編集する）、
 * ダーク・ライトのときはどちらを編集するか
 */
private fun LazyListScope.switchSection(
    config: Config,
    editing: SlotRef,
    activeTime: Int?,
    onEdit: (SlotRef) -> Unit,
    onSwitch: (SwitchBy) -> Unit,
    onTimes: (List<TimeSlot>) -> Unit,
) {
    item {
        Section(stringResource(R.string.switch_section))
        val modes = listOf(SwitchBy.NONE to R.string.switch_none, SwitchBy.TIME to R.string.switch_time, SwitchBy.APPEARANCE to R.string.switch_appearance)
        ConnectedToggles(modes, config.switchBy, onSwitch)
        if (config.switchBy != SwitchBy.NONE) {
            Text(
                stringResource(if (config.switchBy == SwitchBy.TIME) R.string.switch_detail_time else R.string.switch_detail_appearance),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(start = 4.dp, top = 8.dp),
            )
        }
    }
    when (config.switchBy) {
        SwitchBy.NONE -> Unit
        SwitchBy.TIME -> {
            config.timeSlots.forEachIndexed { i, t ->
                item {
                    TimeRow(i, t, config.timeSlots, editing == SlotRef.Time(i), i == activeTime, onEdit, onTimes)
                    Spacer(Modifier.height(ListItemDefaults.SegmentedGap))
                }
            }
            item {
                OutlinedButton(
                    onClick = {
                        // 今編集しているものを写し、一番遅い時刻の1時間後から始める
                        val start = ((config.timeSlots.maxOfOrNull { it.start } ?: 0) + 60) % (24 * 60)
                        onTimes(config.timeSlots + TimeSlot(start, config.mainSlot))
                        onEdit(SlotRef.Time(config.timeSlots.size))
                    },
                    shapes = ButtonDefaults.shapes(),
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text(stringResource(R.string.time_add))
                }
            }
        }
        SwitchBy.APPEARANCE -> item {
            Spacer(Modifier.height(8.dp))
            ConnectedToggles(listOf(SlotRef.Light to R.string.edit_light, SlotRef.Dark to R.string.edit_dark), editing, onEdit)
        }
    }
}

/** 時刻の一覧の1行。押すと選び、「変える」で時刻を選ぶ画面を出す */
@Composable
private fun TimeRow(
    i: Int,
    t: TimeSlot,
    times: List<TimeSlot>,
    selected: Boolean,
    active: Boolean,
    onEdit: (SlotRef) -> Unit,
    onTimes: (List<TimeSlot>) -> Unit,
) {
    var picking by rememberSaveable { mutableStateOf(false) }
    SegmentedListItem(
        selected = selected,
        onClick = { onEdit(SlotRef.Time(i)) },
        shapes = ListItemDefaults.segmentedShapes(i, times.size),
        colors = ListItemDefaults.colors(containerColor = MaterialTheme.colorScheme.surfaceContainer),
        leadingContent = { RadioButton(selected = selected, onClick = null) },
        supportingContent = if (active) ({ Text(stringResource(R.string.time_now)) }) else null,
        trailingContent = {
            Row {
                TextButton(onClick = { picking = true }, shapes = ButtonDefaults.shapes()) { Text(stringResource(R.string.time_change)) }
                if (times.size > 1) {
                    TextButton(
                        onClick = {
                            onTimes(times.filterIndexed { k, _ -> k != i })
                            if (selected) onEdit(SlotRef.Time(maxOf(0, i - 1)))
                        },
                        shapes = ButtonDefaults.shapes(),
                    ) {
                        Text(stringResource(R.string.time_remove))
                    }
                }
            }
        },
    ) {
        Text(stringResource(R.string.time_from, formatClock(t.start)))
    }
    if (picking) {
        val state = rememberTimePickerState(initialHour = t.start / 60, initialMinute = t.start % 60, is24Hour = true)
        TimePickerDialog(
            onDismissRequest = { picking = false },
            confirmButton = {
                TextButton(
                    onClick = {
                        picking = false
                        onTimes(times.mapIndexed { k, s -> if (k == i) s.copy(start = state.hour * 60 + state.minute) else s })
                        onEdit(SlotRef.Time(i))
                    },
                    shapes = ButtonDefaults.shapes(),
                ) {
                    Text(stringResource(R.string.time_ok))
                }
            },
            dismissButton = {
                TextButton(onClick = { picking = false }, shapes = ButtonDefaults.shapes()) { Text(stringResource(R.string.time_cancel)) }
            },
            title = { Text(stringResource(R.string.time_from, formatClock(t.start))) },
        ) {
            TimePicker(state)
        }
    }
}

/** つながったトグルのボタン（1つを選ぶ） */
@Composable
private fun <T> ConnectedToggles(options: List<Pair<T, Int>>, selected: T, onSelect: (T) -> Unit) {
    Row(horizontalArrangement = Arrangement.spacedBy(ButtonGroupDefaults.ConnectedSpaceBetween)) {
        options.forEachIndexed { i, (value, label) ->
            ToggleButton(
                checked = selected == value,
                onCheckedChange = { onSelect(value) },
                shapes = when (i) {
                    0 -> ButtonGroupDefaults.connectedLeadingButtonShapes()
                    options.lastIndex -> ButtonGroupDefaults.connectedTrailingButtonShapes()
                    else -> ButtonGroupDefaults.connectedMiddleButtonShapes()
                },
                contentPadding = PaddingValues(horizontal = 4.dp),
                modifier = Modifier.weight(1f).semantics { role = Role.RadioButton },
            ) {
                Text(stringResource(label), maxLines = 1)
            }
        }
    }
}

@Composable
private fun Section(title: String) {
    Text(
        title,
        style = MaterialTheme.typography.titleSmallEmphasized,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(start = 4.dp, top = 16.dp, bottom = 4.dp),
    )
}

@Composable
private fun ImageCard(
    hasPreview: Boolean,
    imageState: ImageState,
    isActiveWallpaper: Boolean,
    onPickImage: () -> Unit,
    onSetWallpaper: () -> Unit,
) {
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer)) {
        Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(stringResource(R.string.app_name), style = MaterialTheme.typography.titleLargeEmphasized)
            Text(
                stringResource(if (isActiveWallpaper) R.string.status_active else R.string.status_inactive),
                style = MaterialTheme.typography.bodyMedium,
            )
            Text(
                stringResource(
                    when (imageState) {
                        ImageState.NONE -> R.string.image_none
                        ImageState.LOADED -> R.string.image_loaded
                        ImageState.UNREADABLE -> R.string.image_unreadable
                    },
                ),
                style = MaterialTheme.typography.bodyMedium,
                color = if (imageState == ImageState.UNREADABLE) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
            )
            // 主役の操作は「壁紙に設定」（まだのとき）。画像を選ぶのは控えめにする
            if (!isActiveWallpaper && hasPreview) {
                Text(
                    stringResource(R.string.set_wallpaper_detail),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Spacer(Modifier.height(4.dp))
                Button(onClick = onSetWallpaper, shapes = ButtonDefaults.shapes(), modifier = Modifier.fillMaxWidth()) {
                    Text(stringResource(R.string.set_wallpaper))
                }
                OutlinedButton(onClick = onPickImage, shapes = ButtonDefaults.shapes(), modifier = Modifier.fillMaxWidth()) {
                    Text(stringResource(R.string.pick_image))
                }
            } else {
                Spacer(Modifier.height(4.dp))
                Button(onClick = onPickImage, shapes = ButtonDefaults.shapes(), modifier = Modifier.fillMaxWidth()) {
                    Text(stringResource(R.string.pick_image))
                }
            }
        }
    }
}

/**
 * 今のフレームの番号。壁紙と同じ待ち時間と速さで進める。
 * 設定画面が見えていない間は描くための時計が止まるので、進めない
 */
@Composable
private fun rememberFrameIndex(preview: Preview?, speed: Double): State<Int> {
    val index = remember(preview) { mutableIntStateOf(0) }
    LaunchedEffect(preview, speed) {
        val p = preview ?: return@LaunchedEffect
        if (p.frames.size < 2) return@LaunchedEffect
        while (true) {
            delay(frameDelayMillis(p.delaysMillis[index.intValue], speed))
            withFrameNanos { }
            index.intValue = (index.intValue + 1) % p.frames.size
        }
    }
    return index
}

/** 画面の形の枠に、壁紙と同じ見た目を縮めて描く。押すと全画面 */
@Composable
private fun ScreenPreview(
    config: Config,
    preview: Preview?,
    index: State<Int>,
    screen: IntSize,
    openLabel: String,
    onOpen: () -> Unit,
    modifier: Modifier,
) {
    val aspect = if (screen.width > 0 && screen.height > 0) screen.width.toFloat() / screen.height else 9f / 19.5f
    Box(modifier, contentAlignment = Alignment.Center) {
        WallpaperCanvas(
            config,
            preview,
            index,
            screen,
            Modifier
                .aspectRatio(aspect)
                .clip(MaterialTheme.shapes.large)
                .clickable(enabled = preview != null, onClickLabel = openLabel, onClick = onOpen),
        )
    }
}

/**
 * 壁紙が screen の大きさの画面に描くものを、この枠の大きさに縮めて描く（screen が null なら枠がそのまま画面）。
 * 並べ方は壁紙と同じ計算で、ドットはぼかさない。番号は描くときに読むので、フレームが進んでも描き直すだけで済む
 */
@Composable
private fun WallpaperCanvas(config: Config, preview: Preview?, index: State<Int>, screen: IntSize?, modifier: Modifier) {
    val empty = MaterialTheme.colorScheme.surfaceContainerHighest
    Canvas(modifier) {
        val sw = screen?.width?.takeIf { it > 0 } ?: size.width.roundToInt()
        val sh = screen?.height?.takeIf { it > 0 } ?: size.height.roundToInt()
        if (preview == null) {
            drawRect(empty)
            return@Canvas
        }
        drawRect(Color((config.customColor ?: preview.autoBackground) or 0xFF000000.toInt()))
        val image = preview.frames[index.value.coerceIn(0, preview.frames.lastIndex)]
        val r = layout(image.width, image.height, sw, sh, config.placement)
        val s = size.width / sw
        drawImage(
            image,
            dstOffset = IntOffset((r.x * s).roundToInt(), (r.y * s).roundToInt()),
            dstSize = IntSize((r.width * s).roundToInt(), (r.height * s).roundToInt()),
            filterQuality = FilterQuality.None,
        )
    }
}

private fun LazyListScope.fitSection(config: Config, onChange: (Config) -> Unit) {
    val fits = listOf(
        Fit.INTEGER to (R.string.fit_integer to R.string.fit_integer_detail),
        Fit.FIT to (R.string.fit_fit to R.string.fit_fit_detail),
        Fit.FILL to (R.string.fit_fill to R.string.fit_fill_detail),
        Fit.FREE to (R.string.fit_free to R.string.fit_free_detail),
    )
    item { Section(stringResource(R.string.fit)) }
    fits.forEachIndexed { i, (fit, labels) ->
        item {
            SegmentedListItem(
                selected = config.fit == fit,
                onClick = { onChange(config.copy(fit = fit)) },
                shapes = ListItemDefaults.segmentedShapes(i, fits.size),
                leadingContent = { RadioButton(selected = config.fit == fit, onClick = null) },
                supportingContent = { Text(stringResource(labels.second)) },
            ) {
                Text(stringResource(labels.first))
            }
            Spacer(Modifier.height(ListItemDefaults.SegmentedGap))
        }
    }
}

/** 0 で上・左、50 で中央、100 で下・右。1 刻み。動かしている間も壁紙がついてくる */
@Composable
private fun PositionSlider(config: Config, onChange: (Config) -> Unit) {
    PercentSlider(R.string.position_value, config.position, 0..100) { onChange(config.copy(position = it)) }
}

/** 自由に置くときの大きさ（10〜300%）と横・縦の位置（0〜100）。1 刻み。動かしている間も壁紙がついてくる */
@Composable
private fun FreeSliders(config: Config, onChange: (Config) -> Unit) {
    Column {
        PercentSlider(R.string.free_size_value, config.freeSize, 10..300) { onChange(config.copy(freeSize = it)) }
        PercentSlider(R.string.free_x_value, config.freeX, 0..100) { onChange(config.copy(freeX = it)) }
        PercentSlider(R.string.free_y_value, config.freeY, 0..100) { onChange(config.copy(freeY = it)) }
    }
}

/** 0〜1 などの値を % の 1 刻みで動かす。label は % の数を %1$d で受ける */
@Composable
private fun PercentSlider(label: Int, value: Double, range: IntRange, onChange: (Double) -> Unit) {
    val percent = (value * 100).roundToInt()
    Column(Modifier.padding(horizontal = 4.dp)) {
        Text(
            stringResource(label, percent),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Slider(
            value = percent.toFloat(),
            onValueChange = { onChange(it.roundToInt() / 100.0) },
            // 1 刻みに丸める（steps で刻むと目盛りが並びすぎるので使わない）
            valueRange = range.first.toFloat()..range.last.toFloat(),
        )
    }
}

private fun LazyListScope.backgroundSection(config: Config, preview: Preview?, onChange: (Config) -> Unit) {
    item {
        Section(stringResource(R.string.background))
        val options = listOf(false to R.string.background_auto, true to R.string.background_custom)
        Row(horizontalArrangement = Arrangement.spacedBy(ButtonGroupDefaults.ConnectedSpaceBetween)) {
            options.forEachIndexed { i, (custom, label) ->
                ToggleButton(
                    checked = config.customBackground == custom,
                    onCheckedChange = {
                        // 初めて「自分で選ぶ」にしたときは、今の自動の色から始める
                        val color = config.backgroundColor ?: preview?.autoBackground ?: 0
                        onChange(config.copy(customBackground = custom, backgroundColor = if (custom) color else config.backgroundColor))
                    },
                    shapes = if (i == 0) ButtonGroupDefaults.connectedLeadingButtonShapes() else ButtonGroupDefaults.connectedTrailingButtonShapes(),
                    modifier = Modifier.weight(1f).semantics { role = Role.RadioButton },
                ) {
                    Text(stringResource(label))
                }
            }
        }
    }
    if (config.customBackground) {
        item {
            ColorPicker(
                rgb = config.backgroundColor ?: 0,
                onChange = { onChange(config.copy(backgroundColor = it)) },
                modifier = Modifier.padding(horizontal = 4.dp, vertical = 8.dp),
            )
        }
    }
}

@Composable
private fun SpeedButtons(config: Config, onChange: (Config) -> Unit) {
    Row(horizontalArrangement = Arrangement.spacedBy(ButtonGroupDefaults.ConnectedSpaceBetween)) {
        SPEEDS.forEachIndexed { i, speed ->
            ToggleButton(
                checked = config.speed == speed,
                onCheckedChange = { onChange(config.copy(speed = speed)) },
                shapes = when (i) {
                    0 -> ButtonGroupDefaults.connectedLeadingButtonShapes()
                    SPEEDS.lastIndex -> ButtonGroupDefaults.connectedTrailingButtonShapes()
                    else -> ButtonGroupDefaults.connectedMiddleButtonShapes()
                },
                contentPadding = PaddingValues(horizontal = 4.dp),
                modifier = Modifier.weight(1f).semantics { role = Role.RadioButton },
            ) {
                Text(speedLabel(speed), maxLines = 1)
            }
        }
    }
}

private fun speedLabel(speed: Double): String = "×" + if (speed == speed.toInt().toDouble()) speed.toInt().toString() else speed.toString()
