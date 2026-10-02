package io.github.blake12609.scriptbridge.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color

/** The same palette as the Windows app. */
object C {
    val bgTop = Color(0xFF12141A)
    val bg = Color(0xFF0C0D11)
    val card = Color(0xFF171A21)
    val cardBorder = Color(0xFF242933)
    val field = Color(0xFF1F232C)
    val border = Color(0xFF2E3440)
    val text = Color(0xFFE9ECF1)
    val muted = Color(0xFF8A93A3)
    val faint = Color(0xFF5A6371)
    val accent = Color(0xFF5B8CFF)
    val accent2 = Color(0xFF8B5CF6)
    val success = Color(0xFF34D399)
    val warning = Color(0xFFF5B841)
    val danger = Color(0xFFF25F5C)
    val logBg = Color(0xFF0F1115)

    val background = Brush.verticalGradient(listOf(bgTop, bg))
    val accentBrush = Brush.linearGradient(listOf(accent, accent2))
}

@Composable
fun ScriptBridgeTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = C.accent,
            onPrimary = Color.White,
            secondary = C.accent2,
            onSecondary = Color.White,
            background = C.bg,
            onBackground = C.text,
            surface = C.card,
            onSurface = C.text,
            surfaceVariant = C.field,
            onSurfaceVariant = C.muted,
            surfaceContainer = C.field,
            surfaceContainerHigh = C.card,
            surfaceContainerHighest = C.field,
            outline = C.border,
            outlineVariant = C.cardBorder,
            error = C.danger,
        ),
        content = content,
    )
}
