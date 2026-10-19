package xyz.mooma0750.pixwall.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import xyz.mooma0750.pixwall.R
import xyz.mooma0750.pixwall.core.hexColor

/**
 * 色を選ぶ。Compose の Material 3 には色を選ぶ部品が無いので、色相・彩度・明るさの3本のスライダーで選ぶ。
 * 動かしている間も onChange を呼ぶ（壁紙がすぐ変わる）
 */
@Composable
fun ColorPicker(rgb: Int, onChange: (Int) -> Unit, modifier: Modifier = Modifier) {
    // スライダーの値は自分で持つ（色から戻すと、彩度や明るさが 0 のときに色相が飛ぶため）
    // 外から別の色が来たとき（「自動」の色から始めたときなど）だけ、色から戻す
    var held by remember { mutableStateOf(toHsv(rgb)) }
    val hsv = if (fromHsv(held) == rgb) held else toHsv(rgb)

    fun set(next: FloatArray) {
        held = next
        onChange(fromHsv(next))
    }

    Column(modifier, verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            Swatch(rgb)
            Text(hexColor(rgb), style = MaterialTheme.typography.titleMediumEmphasized)
        }
        Labeled(stringResource(R.string.color_hue)) {
            Slider(value = hsv[0], onValueChange = { set(floatArrayOf(it, hsv[1], hsv[2])) }, valueRange = 0f..360f)
        }
        Labeled(stringResource(R.string.color_saturation)) {
            Slider(value = hsv[1], onValueChange = { set(floatArrayOf(hsv[0], it, hsv[2])) })
        }
        Labeled(stringResource(R.string.color_brightness)) {
            Slider(value = hsv[2], onValueChange = { set(floatArrayOf(hsv[0], hsv[1], it)) })
        }
    }
}

@Composable
private fun Labeled(label: String, content: @Composable () -> Unit) {
    Column {
        Text(label, style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
        content()
    }
}

/** 色の見本 */
@Composable
fun Swatch(rgb: Int, modifier: Modifier = Modifier) {
    val shape = MaterialTheme.shapes.medium
    Box(
        modifier
            .size(48.dp)
            .background(Color(rgb or 0xFF000000.toInt()), shape)
            .border(1.dp, MaterialTheme.colorScheme.outlineVariant, shape),
    )
}

private fun toHsv(rgb: Int): FloatArray = FloatArray(3).also { android.graphics.Color.colorToHSV(rgb, it) }

private fun fromHsv(hsv: FloatArray): Int = android.graphics.Color.HSVToColor(hsv) and 0xFFFFFF

