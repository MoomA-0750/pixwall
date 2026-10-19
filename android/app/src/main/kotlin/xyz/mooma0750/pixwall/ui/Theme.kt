package xyz.mooma0750.pixwall.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialExpressiveTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MotionScheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Typography
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight

/**
 * 文字の役割。Expressive の強調（…Emphasized）は既定では Medium で目立たないので、見出しは Bold、本文・ラベルは SemiBold にする
 * （画面では fontWeight を直書きせず、強調はこちらを使う）
 */
private val PixwallTypography: Typography =
    Typography().let { base ->
        base.copy(
            titleLargeEmphasized = base.titleLargeEmphasized.copy(fontWeight = FontWeight.Bold),
            titleMediumEmphasized = base.titleMediumEmphasized.copy(fontWeight = FontWeight.Bold),
            titleSmallEmphasized = base.titleSmallEmphasized.copy(fontWeight = FontWeight.Bold),
            headlineMediumEmphasized = base.headlineMediumEmphasized.copy(fontWeight = FontWeight.Bold),
            labelLargeEmphasized = base.labelLargeEmphasized.copy(fontWeight = FontWeight.SemiBold),
        )
    }

/** 端末の壁紙から作った色（Material You）で、ダークモードにも従う。minSdk 31 なので常に使える。動きは Expressive のばね */
@Composable
fun PixwallTheme(content: @Composable () -> Unit) {
    val context = LocalContext.current
    val colorScheme = if (isSystemInDarkTheme()) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
    MaterialExpressiveTheme(colorScheme = colorScheme, motionScheme = MotionScheme.expressive(), typography = PixwallTypography) {
        Surface(color = MaterialTheme.colorScheme.background, content = content)
    }
}
