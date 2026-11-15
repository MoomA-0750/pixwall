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
import kasane.KasaneShapes
import kasane.kasaneDarkColorScheme
import kasane.kasaneLightColorScheme

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

/**
 * 端末の壁紙から作った色（Material You）で、ダークモードにも従う。minSdk 31 なので常に使える。動きは Expressive のばね。
 * dynamicColor = false（試しの画面など）は kasane の控えの色（アイコンの色が種）。角丸は kasane（KasaneShapes）
 */
@Composable
fun PixwallTheme(dynamicColor: Boolean = true, content: @Composable () -> Unit) {
    val context = LocalContext.current
    val dark = isSystemInDarkTheme()
    val colorScheme = when {
        dynamicColor -> if (dark) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        dark -> kasaneDarkColorScheme()
        else -> kasaneLightColorScheme()
    }
    MaterialExpressiveTheme(
        colorScheme = colorScheme,
        motionScheme = MotionScheme.expressive(),
        shapes = KasaneShapes,
        typography = PixwallTypography,
    ) {
        Surface(color = MaterialTheme.colorScheme.background, content = content)
    }
}
