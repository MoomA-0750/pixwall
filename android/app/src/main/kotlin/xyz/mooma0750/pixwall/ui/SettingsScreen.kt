package xyz.mooma0750.pixwall.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ButtonGroupDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedListItem
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.ToggleButton
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import xyz.mooma0750.pixwall.R
import xyz.mooma0750.pixwall.core.Config
import xyz.mooma0750.pixwall.core.Fit
import xyz.mooma0750.pixwall.core.SPEEDS
import xyz.mooma0750.pixwall.core.layout
import kotlin.math.roundToInt

/** 設定画面に出す画像の1枚目と、「自動」の余白の色 */
class Preview(val image: ImageBitmap, val autoBackground: Int)

/** 画像の状態 */
enum class ImageState { NONE, LOADED, UNREADABLE }

@Composable
fun SettingsScreen(
    config: Config,
    preview: Preview?,
    imageState: ImageState,
    isActiveWallpaper: Boolean,
    screenAspect: Float,
    onPickImage: () -> Unit,
    onSetWallpaper: () -> Unit,
    onChange: (Config) -> Unit,
) {
    val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()
    Scaffold(
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        topBar = {
            LargeFlexibleTopAppBar(
                title = { Text(stringResource(R.string.app_name)) },
                subtitle = {
                    Text(stringResource(if (isActiveWallpaper) R.string.status_active else R.string.status_inactive))
                },
                scrollBehavior = scrollBehavior,
            )
        },
    ) { padding ->
        LazyColumn(
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = padding.calculateTopPadding(), bottom = padding.calculateBottomPadding() + 24.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            item {
                ImageCard(config, preview, imageState, isActiveWallpaper, screenAspect, onPickImage, onSetWallpaper)
            }
            fitSection(config, onChange)
            if (config.fit != Fit.INTEGER) {
                item {
                    Section(stringResource(R.string.position))
                    PositionSlider(config, onChange)
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
    config: Config,
    preview: Preview?,
    imageState: ImageState,
    isActiveWallpaper: Boolean,
    screenAspect: Float,
    onPickImage: () -> Unit,
    onSetWallpaper: () -> Unit,
) {
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer)) {
        Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                ScreenPreview(config, preview, screenAspect, Modifier.widthIn(max = 120.dp))
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
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
                    if (!isActiveWallpaper && imageState == ImageState.LOADED) {
                        Text(
                            stringResource(R.string.set_wallpaper_detail),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
            // 主役の操作は「壁紙に設定」（まだのとき）。画像を選ぶのは控えめにする
            if (!isActiveWallpaper && imageState == ImageState.LOADED) {
                Button(onClick = onSetWallpaper, shapes = ButtonDefaults.shapes(), modifier = Modifier.fillMaxWidth()) {
                    Text(stringResource(R.string.set_wallpaper))
                }
                OutlinedButton(onClick = onPickImage, shapes = ButtonDefaults.shapes(), modifier = Modifier.fillMaxWidth()) {
                    Text(stringResource(R.string.pick_image))
                }
            } else {
                Button(onClick = onPickImage, shapes = ButtonDefaults.shapes(), modifier = Modifier.fillMaxWidth()) {
                    Text(stringResource(R.string.pick_image))
                }
            }
        }
    }
}

/** 画面の縦横比の枠に、壁紙と同じ並べ方・位置・余白の色で1枚目を描く。ドットはぼかさない */
@Composable
private fun ScreenPreview(config: Config, preview: Preview?, screenAspect: Float, modifier: Modifier = Modifier) {
    val background = preview?.let { Color((config.customColor ?: it.autoBackground) or 0xFF000000.toInt()) }
        ?: MaterialTheme.colorScheme.surfaceContainerHighest
    Canvas(
        modifier
            .fillMaxWidth()
            .aspectRatio(screenAspect)
            .clip(MaterialTheme.shapes.medium),
    ) {
        drawRect(background)
        val image = preview?.image ?: return@Canvas
        val r = layout(image.width, image.height, size.width.roundToInt(), size.height.roundToInt(), config.fit, config.position)
        drawImage(
            image,
            dstOffset = IntOffset(r.x, r.y),
            dstSize = IntSize(r.width, r.height),
            filterQuality = FilterQuality.None,
        )
    }
}

private fun LazyListScope.fitSection(config: Config, onChange: (Config) -> Unit) {
    val fits = listOf(
        Fit.INTEGER to (R.string.fit_integer to R.string.fit_integer_detail),
        Fit.FIT to (R.string.fit_fit to R.string.fit_fit_detail),
        Fit.FILL to (R.string.fit_fill to R.string.fit_fill_detail),
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
    val percent = (config.position * 100).roundToInt()
    Column(Modifier.padding(horizontal = 4.dp)) {
        Text(
            stringResource(R.string.position_value, percent),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Slider(
            value = percent.toFloat(),
            onValueChange = { onChange(config.copy(position = it.roundToInt() / 100.0)) },
            // 1 刻みに丸める（steps で刻むと目盛りが 99 個並ぶので使わない）
            valueRange = 0f..100f,
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
