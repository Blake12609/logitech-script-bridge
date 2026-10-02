package io.github.blake12609.scriptbridge.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** Rounded card with a small spaced-out caption, like the Windows app's cards. */
@Composable
fun SectionCard(
    title: String,
    modifier: Modifier = Modifier,
    trailing: @Composable () -> Unit = {},
    content: @Composable ColumnScope.() -> Unit,
) {
    val shape = RoundedCornerShape(18.dp)
    Column(
        modifier
            .fillMaxWidth()
            .clip(shape)
            .background(C.card)
            .border(1.dp, C.cardBorder, shape)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Caption(title, Modifier.weight(1f))
            trailing()
        }
        content()
    }
}

@Composable
fun Caption(text: String, modifier: Modifier = Modifier, color: Color = C.muted) {
    Text(
        text.uppercase(),
        modifier = modifier,
        color = color,
        fontSize = 11.sp,
        fontWeight = FontWeight.SemiBold,
        letterSpacing = 1.4.sp,
    )
}

@Composable
fun Dot(color: Color, size: Dp = 8.dp) {
    Box(Modifier.size(size).clip(RoundedCornerShape(50)).background(color))
}

/** A field-styled button that opens a list. */
@Composable
fun <T> Dropdown(
    options: List<T>,
    selected: T,
    label: (T) -> String,
    onSelect: (T) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    var open by remember { mutableStateOf(false) }
    val shape = RoundedCornerShape(12.dp)
    Box(modifier) {
        Row(
            Modifier
                .fillMaxWidth()
                .clip(shape)
                .background(C.field)
                .border(1.dp, C.border, shape)
                .clickable(enabled = enabled) { open = true }
                .padding(horizontal = 14.dp, vertical = 13.dp)
                .alpha(if (enabled) 1f else 0.5f),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(label(selected), Modifier.weight(1f), color = C.text, fontSize = 15.sp, maxLines = 1,
                overflow = TextOverflow.Ellipsis)
            Icon(Icons.Rounded.ExpandMore, null, tint = C.muted)
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            options.forEach { option ->
                DropdownMenuItem(
                    text = { Text(label(option), color = if (option == selected) C.accent else C.text) },
                    onClick = {
                        open = false
                        onSelect(option)
                    },
                )
            }
        }
    }
}

@Composable
fun NumberField(
    value: String,
    onChange: (String) -> Unit,
    label: String,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    decimals: Boolean = true,
) {
    OutlinedTextField(
        value = value,
        onValueChange = { s -> if (s.length <= 8) onChange(s.filter { it.isDigit() || (decimals && (it == '.' || it == ',')) }) },
        label = { Text(label) },
        modifier = modifier,
        enabled = enabled,
        singleLine = true,
        shape = RoundedCornerShape(12.dp),
        keyboardOptions = KeyboardOptions(keyboardType = if (decimals) KeyboardType.Decimal else KeyboardType.Number),
        colors = OutlinedTextFieldDefaults.colors(
            focusedTextColor = C.text,
            unfocusedTextColor = C.text,
            disabledTextColor = C.muted,
            focusedContainerColor = C.field,
            unfocusedContainerColor = C.field,
            disabledContainerColor = C.field,
            focusedBorderColor = C.accent,
            unfocusedBorderColor = C.border,
            disabledBorderColor = C.cardBorder,
            focusedLabelColor = C.accent,
            unfocusedLabelColor = C.muted,
            disabledLabelColor = C.faint,
            cursorColor = C.accent,
        ),
    )
}

/** Icon-over-label button used in the hero card. */
@Composable
fun ActionButton(
    icon: ImageVector,
    label: String,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    val shape = RoundedCornerShape(12.dp)
    Column(
        modifier
            .clip(shape)
            .background(Color.White.copy(alpha = 0.05f))
            .clickable(enabled = enabled, onClick = onClick)
            .padding(vertical = 10.dp)
            .alpha(if (enabled) 1f else 0.4f),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Icon(icon, null, tint = C.text, modifier = Modifier.size(20.dp))
        Text(label, color = C.text, fontSize = 12.sp)
    }
}

/** The app logo: a mouse on the accent gradient. */
@Composable
fun Logo(size: Dp) {
    Box(
        Modifier
            .size(size)
            .clip(RoundedCornerShape(size * 0.28f))
            .background(C.accentBrush),
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val s = this.size.width
            val stroke = s / 14
            val w = s * 0.34f
            val h = s * 0.6f
            val x = s * 0.33f
            val y = s * 0.2f
            drawRoundRect(Color.White, Offset(x, y), Size(w, h), CornerRadius(w / 2), style = Stroke(stroke))
            drawLine(Color.White, Offset(x + w / 2, y + h * 0.12f), Offset(x + w / 2, y + h * 0.34f), stroke,
                StrokeCap.Round)
        }
    }
}

@Composable
fun Hint(text: String, modifier: Modifier = Modifier, icon: ImageVector? = null, color: Color = C.accent) {
    val shape = RoundedCornerShape(12.dp)
    Row(
        modifier
            .fillMaxWidth()
            .clip(shape)
            .background(color.copy(alpha = 0.08f))
            .border(1.dp, color.copy(alpha = 0.25f), shape)
            .padding(12.dp),
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        if (icon != null) Icon(icon, null, tint = color, modifier = Modifier.size(18.dp))
        Text(text, color = C.text.copy(alpha = 0.85f), fontSize = 13.sp, lineHeight = 18.sp)
    }
}

@Composable
fun Gap(height: Dp) = Spacer(Modifier.size(height))
