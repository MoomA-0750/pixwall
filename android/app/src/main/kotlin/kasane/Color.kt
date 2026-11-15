// 書き出したもの。直すのは kasane の tokens.css と themes/personal.css（scripts/build.mjs）
// kasane 1.1.0  source 5cf2e411d01aaae4  seed #FF5A7A #1E1B2E
package kasane

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.ui.graphics.Color

/** 動的色が無いとき（Android 11 以前）の控えの色。kasane の personal を種の色から導いたもの */
fun kasaneLightColorScheme(): ColorScheme = lightColorScheme(
    primary = Color(0xFFB93D56),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFF3E2E6),
    onPrimaryContainer = Color(0xFFA12543),
    error = Color(0xFFC52940),
    onError = Color(0xFFFFFFFF),
    errorContainer = Color(0xFFF7E7EB),
    onErrorContainer = Color(0xFFC52940),
    background = Color(0xFFF4F4F7),
    onBackground = Color(0xFF2B2B2F),
    surface = Color(0xFFFDFDFE),
    onSurface = Color(0xFF2B2B2F),
    surfaceVariant = Color(0xFFECECF0),
    onSurfaceVariant = Color(0xFF5A5A61),
    outline = Color(0xFF84848B),
    outlineVariant = Color(0xFFDCDCE0),
)

fun kasaneDarkColorScheme(): ColorScheme = darkColorScheme(
    primary = Color(0xFFE77686),
    onPrimary = Color(0xFF250E11),
    primaryContainer = Color(0xFF3B2B2F),
    onPrimaryContainer = Color(0xFFFF8C9C),
    error = Color(0xFFFF757C),
    onError = Color(0xFF2B0B09),
    errorContainer = Color(0xFF36282A),
    onErrorContainer = Color(0xFFFF757C),
    background = Color(0xFF171719),
    onBackground = Color(0xFFE7E7EA),
    surface = Color(0xFF1F1F21),
    onSurface = Color(0xFFE7E7EA),
    surfaceVariant = Color(0xFF262629),
    onSurfaceVariant = Color(0xFFB0B0B5),
    outline = Color(0xFF656568),
    outlineVariant = Color(0xFF2D2D2F),
)
