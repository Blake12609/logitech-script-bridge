package io.github.blake12609.scriptbridge.ui

import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.Share
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.blake12609.scriptbridge.Bridge
import io.github.blake12609.scriptbridge.NativeBridge
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private val codeStyle = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 14.sp, lineHeight = 21.sp, color = C.text)

// Keys that are awkward to reach on a phone keyboard.
private val extraKeys = listOf(
    "⇥" to "    ", "(" to "(", ")" to ")", "\"" to "\"", "=" to "=", "==" to " == ", "~=" to " ~= ",
    "," to ", ", "." to ".", ":" to ":", "[" to "[", "]" to "]", "{" to "{", "}" to "}", "#" to "#",
    "--" to "-- ", "end" to "end", "then" to " then", "and" to " and ", "not" to "not ", "Sleep" to "Sleep(10)",
)

@Composable
fun EditorScreen(name: String, onClose: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val density = LocalDensity.current
    var value by remember(name) { mutableStateOf(TextFieldValue(Bridge.readScript(name))) }
    var saved by remember(name) { mutableStateOf(value.text) }
    var problem by remember { mutableStateOf("") }
    var askDiscard by remember { mutableStateOf(false) }
    val dirty = value.text != saved
    val vScroll = rememberScrollState()
    val hScroll = rememberScrollState()

    // live syntax check, a moment after typing stops
    LaunchedEffect(value.text) {
        delay(250)
        val text = value.text
        problem = withContext(Dispatchers.Default) {
            String(NativeBridge.checkSyntax(text.toByteArray(), name.toByteArray()), Charsets.UTF_8)
        }
    }
    val errorLine = Regex(":(\\d+):").find(problem)?.groupValues?.get(1)?.toIntOrNull() ?: 0
    val message = problem.replaceFirst(Regex("^.*?:\\d+: "), "")

    fun save(): Boolean {
        if (!Bridge.save(name, value.text)) return false
        saved = value.text
        return true
    }

    fun goToLine(line: Int) {
        value = value.copy(selection = TextRange(lineOffset(value.text, line)))
        val px = with(density) { codeStyle.lineHeight.toPx() } * (line - 1)
        scope.launch { vScroll.animateScrollTo((px - 200).toInt().coerceAtLeast(0)) }
    }

    val exporter = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        if (uri != null) runCatching {
            context.contentResolver.openOutputStream(uri)?.use { it.write(value.text.toByteArray()) }
        }
    }

    BackHandler { if (dirty) askDiscard = true else onClose() }

    Column(
        Modifier
            .fillMaxSize()
            .background(C.bg)
            .windowInsetsPadding(WindowInsets.safeDrawing),
    ) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = { if (dirty) askDiscard = true else onClose() }) {
                Icon(Icons.AutoMirrored.Rounded.ArrowBack, "Back", tint = C.text)
            }
            Column(Modifier.weight(1f)) {
                Text(name, color = C.text, fontSize = 16.sp, fontWeight = FontWeight.SemiBold, maxLines = 1,
                    overflow = TextOverflow.Ellipsis)
                val running = Bridge.running && name == Bridge.script
                Text(
                    when {
                        dirty && running -> "Unsaved · saving restarts the script"
                        dirty -> "Unsaved changes"
                        running -> "Running"
                        else -> "Saved"
                    },
                    color = if (dirty) C.accent else C.muted, fontSize = 12.sp,
                )
            }
            IconButton(onClick = { exporter.launch(name) }) { Icon(Icons.Rounded.Share, "Save a copy", tint = C.muted) }
            Button(
                onClick = { save() },
                enabled = dirty,
                shape = RoundedCornerShape(12.dp),
                colors = ButtonDefaults.buttonColors(containerColor = C.accent, disabledContainerColor = C.field),
                modifier = Modifier.padding(end = 8.dp),
            ) {
                Icon(Icons.Rounded.Save, null, modifier = Modifier.size(18.dp))
                Text("Save", modifier = Modifier.padding(start = 6.dp))
            }
        }

        // syntax status
        Row(
            Modifier
                .fillMaxWidth()
                .background(C.card)
                .clickable(enabled = errorLine > 0) { goToLine(errorLine) }
                .padding(horizontal = 16.dp, vertical = 9.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            val ok = problem.isEmpty()
            Icon(
                if (ok) Icons.Rounded.CheckCircle else Icons.Rounded.ErrorOutline, null,
                tint = if (ok) C.success else C.danger, modifier = Modifier.size(16.dp),
            )
            Text(
                when {
                    ok && value.text.isBlank() -> "Empty script"
                    ok -> "No syntax errors"
                    errorLine > 0 -> "Line $errorLine: $message"
                    else -> message
                },
                color = if (ok) C.muted else C.danger, fontSize = 13.sp, maxLines = 2, overflow = TextOverflow.Ellipsis,
            )
        }

        BoxWithConstraints(Modifier.weight(1f).fillMaxWidth().background(C.logBg)) {
            val gutter = 46.dp
            val viewportWidth = maxWidth
            val viewportHeight = maxHeight
            Row(Modifier.fillMaxSize().verticalScroll(vScroll)) {
                val lines = value.text.count { it == '\n' } + 1
                Text(
                    buildAnnotatedString {
                        for (i in 1..lines) {
                            withStyle(SpanStyle(color = if (i == errorLine) C.danger else C.faint)) { append(i.toString()) }
                            if (i < lines) append('\n')
                        }
                    },
                    style = codeStyle.copy(textAlign = TextAlign.End),
                    modifier = Modifier.width(gutter).padding(top = 10.dp, end = 8.dp),
                )
                Box(Modifier.horizontalScroll(hScroll)) {
                    BasicTextField(
                        value = value,
                        onValueChange = { value = autoIndent(value, it) },
                        textStyle = codeStyle,
                        cursorBrush = SolidColor(C.accent),
                        visualTransformation = LuaHighlighter,
                        keyboardOptions = KeyboardOptions(
                            capitalization = KeyboardCapitalization.None,
                            keyboardType = KeyboardType.Ascii,
                        ),
                        modifier = Modifier
                            .widthIn(min = viewportWidth - gutter)
                            .heightIn(min = viewportHeight)
                            .padding(top = 10.dp, end = 16.dp, bottom = 120.dp),
                    )
                }
            }
        }

        Row(
            Modifier
                .fillMaxWidth()
                .background(C.card)
                .horizontalScroll(rememberScrollState())
                .padding(horizontal = 8.dp, vertical = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            for ((label, text) in extraKeys) {
                Box(
                    Modifier
                        .clip(RoundedCornerShape(8.dp))
                        .background(C.field)
                        .clickable { value = insertText(value, text, if (text.endsWith("(10)")) 1 else 0) }
                        .padding(horizontal = 13.dp, vertical = 9.dp),
                ) {
                    Text(label, color = C.text, fontFamily = FontFamily.Monospace, fontSize = 14.sp)
                }
            }
        }
    }

    if (askDiscard) {
        AlertDialog(
            onDismissRequest = { askDiscard = false },
            title = { Text("Unsaved changes") },
            text = { Text("Save your changes to $name?") },
            confirmButton = {
                TextButton(onClick = {
                    askDiscard = false
                    if (save()) onClose()
                }) { Text("Save") }
            },
            dismissButton = {
                Row {
                    TextButton(onClick = {
                        askDiscard = false
                        onClose()
                    }) { Text("Discard", color = C.danger) }
                    TextButton(onClick = { askDiscard = false }) { Text("Cancel") }
                }
            },
        )
    }
}
