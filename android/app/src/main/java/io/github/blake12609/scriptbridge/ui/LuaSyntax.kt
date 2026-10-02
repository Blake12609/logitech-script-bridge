package io.github.blake12609.scriptbridge.ui

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.input.OffsetMapping
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.input.TransformedText
import androidx.compose.ui.text.input.VisualTransformation

// Same colours as the Windows editor.
private val keywordStyle = SpanStyle(color = Color(0xFFC792EA))
private val apiStyle = SpanStyle(color = Color(0xFF82AAFF))
private val builtinStyle = SpanStyle(color = Color(0xFF89DDFF))
private val stringStyle = SpanStyle(color = Color(0xFFC3E88D))
private val numberStyle = SpanStyle(color = Color(0xFFF78C6C))
private val commentStyle = SpanStyle(color = Color(0xFF6B7385), fontStyle = FontStyle.Italic)

private val keywords = setOf(
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
    "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while",
)

private val api = setOf(
    "OnEvent", "GetMKeyState", "SetMKeyState", "Sleep", "OutputLogMessage", "GetRunningTime", "GetDate",
    "ClearLog", "PressKey", "ReleaseKey", "PressAndReleaseKey", "IsModifierPressed", "PressMouseButton",
    "ReleaseMouseButton", "PressAndReleaseMouseButton", "IsMouseButtonPressed", "MoveMouseTo",
    "MoveMouseWheel", "MoveMouseRelative", "MoveMouseToVirtual", "GetMousePosition", "OutputLCDMessage",
    "ClearLCD", "PlayMacro", "PressMacro", "ReleaseMacro", "AbortMacro", "IsKeyLockOn", "SetBacklightColor",
    "OutputDebugMessage", "SetMouseDPITable", "SetMouseDPITableIndex", "EnablePrimaryMouseButtonEvents",
    "EnableHidEvents", "SetSteeringWheelProperty",
)

private val builtins = setOf(
    "string", "table", "math", "os", "coroutine", "utf8", "print", "pairs", "ipairs", "next", "type",
    "tostring", "tonumber", "select", "pcall", "xpcall", "error", "assert", "unpack", "setmetatable",
    "getmetatable", "rawget", "rawset", "rawequal", "rawlen", "load", "loadstring", "self",
)

/** Colours Lua source for the editor. */
fun highlightLua(s: String): AnnotatedString = buildAnnotatedString {
    append(s)
    var i = 0
    val n = s.length
    fun endOf(token: String, from: Int) = s.indexOf(token, from).let { if (it < 0) n else it + token.length }
    while (i < n) {
        val c = s[i]
        when {
            c == '-' && s.startsWith("--", i) -> {
                val end = if (s.startsWith("--[[", i)) endOf("]]", i + 4) else s.indexOf('\n', i).let { if (it < 0) n else it }
                addStyle(commentStyle, i, end)
                i = end
            }
            c == '[' && s.startsWith("[[", i) -> {
                val end = endOf("]]", i + 2)
                addStyle(stringStyle, i, end)
                i = end
            }
            c == '"' || c == '\'' -> {
                var j = i + 1
                while (j < n && s[j] != c && s[j] != '\n') j += if (s[j] == '\\') 2 else 1
                val end = minOf(j + 1, n)
                addStyle(stringStyle, i, end)
                i = end
            }
            c.isDigit() -> {
                var j = i + 1
                while (j < n && (s[j].isLetterOrDigit() || s[j] == '.')) j++
                addStyle(numberStyle, i, j)
                i = j
            }
            c.isLetter() || c == '_' -> {
                var j = i + 1
                while (j < n && (s[j].isLetterOrDigit() || s[j] == '_')) j++
                when (s.substring(i, j)) {
                    in keywords -> addStyle(keywordStyle, i, j)
                    in api -> addStyle(apiStyle, i, j)
                    in builtins -> addStyle(builtinStyle, i, j)
                }
                i = j
            }
            else -> i++
        }
    }
}

object LuaHighlighter : VisualTransformation {
    override fun filter(text: AnnotatedString) = TransformedText(highlightLua(text.text), OffsetMapping.Identity)
}

/** After Enter: keep the previous line's indent, one step more after a line that opens a block. */
fun autoIndent(old: TextFieldValue, new: TextFieldValue): TextFieldValue {
    val pos = old.selection.start
    if (!old.selection.collapsed || !new.selection.collapsed || new.text.length != old.text.length + 1) return new
    if (new.selection.start != pos + 1 || new.text[pos] != '\n' || !new.text.startsWith(old.text.substring(0, pos)))
        return new
    val lineStart = old.text.lastIndexOf('\n', pos - 1) + 1
    val line = old.text.substring(lineStart, pos)
    var indent = line.takeWhile { it == ' ' || it == '\t' }
    if (opensBlock(line.trimEnd())) indent += "    "
    if (indent.isEmpty()) return new
    val text = new.text.substring(0, pos + 1) + indent + new.text.substring(pos + 1)
    return TextFieldValue(text, TextRange(pos + 1 + indent.length))
}

private fun opensBlock(t: String): Boolean {
    fun endsWithWord(w: String) =
        t.endsWith(w) && (t.length == w.length || !(t[t.length - w.length - 1].isLetterOrDigit() || t[t.length - w.length - 1] == '_'))
    return endsWithWord("then") || endsWithWord("do") || endsWithWord("else") || endsWithWord("repeat") ||
        (t.contains("function") && t.endsWith(")")) || t.endsWith("{")
}

/** Types [s] over the selection; the caret ends [back] characters before its end. */
fun insertText(v: TextFieldValue, s: String, back: Int = 0): TextFieldValue {
    val start = v.selection.min
    val text = v.text.replaceRange(start, v.selection.max, s)
    return TextFieldValue(text, TextRange(start + s.length - back))
}

/** Offset of the first character of a 1-based line. */
fun lineOffset(text: String, line: Int): Int {
    var offset = 0
    repeat(line - 1) {
        val nl = text.indexOf('\n', offset)
        if (nl < 0) return offset
        offset = nl + 1
    }
    return offset
}
