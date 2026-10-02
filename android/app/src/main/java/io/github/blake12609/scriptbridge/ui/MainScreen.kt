package io.github.blake12609.scriptbridge.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.List
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.Edit
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Stop
import androidx.compose.material.icons.rounded.Usb
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.blake12609.scriptbridge.Bridge
import io.github.blake12609.scriptbridge.BuildConfig
import io.github.blake12609.scriptbridge.LogKind
import io.github.blake12609.scriptbridge.NativeBridge

@Composable
fun MainScreen(mask: Int, onStart: () -> Unit, onEdit: (String) -> Unit) {
    var showScripts by remember { mutableStateOf(false) }
    var showNew by remember { mutableStateOf(false) }
    val importer = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) Bridge.import(uri)
    }

    Column(
        Modifier
            .fillMaxSize()
            .background(C.background)
            .windowInsetsPadding(WindowInsets.safeDrawing)
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Header()
        Hero(
            onStart = onStart,
            onOpen = { importer.launch(arrayOf("*/*")) },
            onScripts = { showScripts = true },
            onNew = { showNew = true },
            onEdit = { Bridge.script?.let(onEdit) },
        )
        DeviceCard()
        MouseCard(mask)
        OptionsCard()
        LogCard()
        Text(
            "Scripts run on this phone and go out through the device, so the PC needs no software.",
            color = C.faint, fontSize = 12.sp, modifier = Modifier.padding(horizontal = 4.dp, vertical = 4.dp),
        )
    }

    if (showScripts) ScriptsDialog(onDismiss = { showScripts = false })
    if (showNew) NewScriptDialog(onDismiss = { showNew = false }, onCreated = { showNew = false; onEdit(it) })
}

@Composable
private fun Header() {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        Logo(30.dp)
        Text("Script Bridge", color = C.text, fontSize = 18.sp, fontWeight = FontWeight.SemiBold)
        Text("v${BuildConfig.VERSION_NAME}", color = C.faint, fontSize = 12.sp, modifier = Modifier.weight(1f))
        LinkPill()
    }
}

@Composable
private fun LinkPill() {
    val (color, text) = when {
        Bridge.device == NativeBridge.DEMO -> C.accent to "Demo"
        Bridge.link == Bridge.Link.Connected -> C.success to "Connected"
        Bridge.link == Bridge.Link.Waiting -> C.warning to "Allow USB"
        else -> C.faint to "Not connected"
    }
    Row(
        Modifier
            .clip(RoundedCornerShape(50))
            .background(C.card)
            .border(1.dp, C.cardBorder, RoundedCornerShape(50))
            .padding(horizontal = 10.dp, vertical = 5.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Dot(color, 7.dp)
        Text(text, color = C.text, fontSize = 12.sp)
    }
}

@Composable
private fun Hero(onStart: () -> Unit, onOpen: () -> Unit, onScripts: () -> Unit, onNew: () -> Unit, onEdit: () -> Unit) {
    val running = Bridge.running
    val shape = RoundedCornerShape(22.dp)
    Column(
        Modifier
            .fillMaxWidth()
            .clip(shape)
            .background(Brush.linearGradient(listOf(Color(0xFF1B2540), Color(0xFF221B3B))))
            .border(1.dp, if (running) C.success.copy(alpha = 0.45f) else C.cardBorder, shape)
            .padding(18.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                StatusBadge(running)
                Text(
                    Bridge.script?.removeSuffix(".lua") ?: "No script",
                    color = C.text, fontSize = 24.sp, fontWeight = FontWeight.Bold,
                    maxLines = 1, overflow = TextOverflow.Ellipsis,
                )
                Text(subtitle(), color = C.muted, fontSize = 13.sp, lineHeight = 17.sp)
            }
            StartButton(running, Bridge.busy, onStart)
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            val idle = !running
            ActionButton(Icons.Rounded.FolderOpen, "Open", Modifier.weight(1f), idle, onOpen)
            ActionButton(Icons.AutoMirrored.Rounded.List, "Scripts", Modifier.weight(1f), idle, onScripts)
            ActionButton(Icons.Rounded.Add, "New", Modifier.weight(1f), idle, onNew)
            ActionButton(Icons.Rounded.Edit, "Edit", Modifier.weight(1f), Bridge.script != null, onEdit)
        }
    }
}

private fun subtitle(): String {
    val parts = mutableListOf(Bridge.deviceInfo.label.substringBefore(" ("))
    val max = Bridge.jitterMax.replace(',', '.').toDoubleOrNull() ?: 0.0
    if (max > 0) parts += "wobble ${Bridge.jitterMin.ifBlank { "0" }}–${Bridge.jitterMax} px"
    if (Bridge.errorLine > 0) parts += "error on line ${Bridge.errorLine}"
    return parts.joinToString("  ·  ")
}

@Composable
private fun StatusBadge(running: Boolean) {
    val color = if (running) C.success else C.muted
    Row(
        Modifier
            .clip(RoundedCornerShape(50))
            .background(color.copy(alpha = 0.12f))
            .border(1.dp, color.copy(alpha = 0.35f), RoundedCornerShape(50))
            .padding(horizontal = 9.dp, vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Dot(color, 6.dp)
        Text(if (running) "RUNNING" else "STOPPED", color = color, fontSize = 11.sp, fontWeight = FontWeight.Bold,
            letterSpacing = 1.sp)
    }
}

@Composable
private fun StartButton(running: Boolean, busy: Boolean, onClick: () -> Unit) {
    val pulse by rememberInfiniteTransition(label = "pulse").animateFloat(
        initialValue = 1f, targetValue = 1.18f,
        animationSpec = infiniteRepeatable(tween(900), RepeatMode.Reverse), label = "ring",
    )
    Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.padding(start = 12.dp)) {
        Box(Modifier.size(96.dp), contentAlignment = Alignment.Center) {
            if (running) {
                Box(
                    Modifier
                        .size(80.dp)
                        .scale(pulse)
                        .clip(CircleShape)
                        .border(2.dp, C.danger.copy(alpha = 0.35f), CircleShape),
                )
            }
            Box(
                Modifier
                    .size(78.dp)
                    .clip(CircleShape)
                    .background(
                        if (running) Brush.linearGradient(listOf(C.danger, Color(0xFFD9468F))) else C.accentBrush,
                    )
                    .border(1.dp, Color.White.copy(alpha = 0.18f), CircleShape)
                    .clickable(enabled = !busy, onClick = onClick)
                    .alpha(if (busy) 0.6f else 1f),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    if (running) Icons.Rounded.Stop else Icons.Rounded.PlayArrow,
                    contentDescription = if (running) "Stop" else "Start",
                    tint = Color.White, modifier = Modifier.size(40.dp),
                )
            }
        }
        Text(if (running) "Stop" else "Start", color = C.text, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
    }
}

// ------------------------------------------------------------------ device

@Composable
private fun DeviceCard() {
    val info = Bridge.deviceInfo
    SectionCard("Output device") {
        Dropdown(
            options = Bridge.devices, selected = info, label = { it.label },
            onSelect = { Bridge.selectDevice(it.id) }, enabled = !Bridge.running,
        )
        if (info.serial) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                val connected = Bridge.link == Bridge.Link.Connected
                Icon(Icons.Rounded.Usb, null, tint = if (connected) C.success else C.muted, modifier = Modifier.size(20.dp))
                Text(Bridge.linkText, color = if (connected) C.text else C.muted, fontSize = 13.sp,
                    modifier = Modifier.weight(1f), maxLines = 2, overflow = TextOverflow.Ellipsis)
                if (Bridge.link == Bridge.Link.Off) {
                    Button(
                        onClick = { Bridge.connect() },
                        colors = ButtonDefaults.buttonColors(containerColor = C.accent),
                        shape = RoundedCornerShape(12.dp),
                    ) { Text("Connect") }
                } else {
                    OutlinedButton(
                        onClick = { Bridge.disconnect() },
                        enabled = !Bridge.running,
                        shape = RoundedCornerShape(12.dp),
                    ) { Text("Disconnect", color = C.text) }
                }
            }
            if (info.id != NativeBridge.MAKCU) Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Text("Baud", color = C.muted, fontSize = 13.sp, modifier = Modifier.width(52.dp))
                Dropdown(
                    options = Bridge.baudRates, selected = Bridge.baud, label = { it.toString() },
                    onSelect = { Bridge.selectBaud(it) }, enabled = Bridge.link == Bridge.Link.Off,
                    modifier = Modifier.weight(1f),
                )
            }
        }
        Hint(info.hint, icon = Icons.Rounded.Info)
    }
}

// ------------------------------------------------------------------ mouse

@Composable
private fun MouseCard(mask: Int) {
    SectionCard(
        "Mouse",
        trailing = {
            if (Bridge.running) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                    Dot(C.success, 6.dp)
                    Caption("Live", color = C.success)
                }
            } else {
                Caption("Idle", color = C.faint)
            }
        },
    ) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center) {
            Column(Modifier.padding(top = 70.dp, end = 6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                PadButton(5, "5", mask, Modifier.size(width = 24.dp, height = 40.dp), RoundedCornerShape(9.dp), bordered = true)
                PadButton(4, "4", mask, Modifier.size(width = 24.dp, height = 40.dp), RoundedCornerShape(9.dp), bordered = true)
            }
            val body = RoundedCornerShape(topStart = 80.dp, topEnd = 80.dp, bottomStart = 70.dp, bottomEnd = 70.dp)
            Column(
                Modifier
                    .width(156.dp)
                    .clip(body)
                    .background(C.field)
                    .border(1.dp, C.border, body),
            ) {
                Row(Modifier.height(104.dp)) {
                    PadButton(1, "1", mask, Modifier.weight(1f).fillMaxHeight(), RectangleShape)
                    Box(Modifier.width(1.dp).fillMaxHeight().background(C.border))
                    PadButton(3, "3", mask, Modifier.width(30.dp).fillMaxHeight(), RectangleShape)
                    Box(Modifier.width(1.dp).fillMaxHeight().background(C.border))
                    PadButton(2, "2", mask, Modifier.weight(1f).fillMaxHeight(), RectangleShape)
                }
                Box(Modifier.fillMaxWidth().height(1.dp).background(C.border))
                Box(Modifier.fillMaxWidth().height(84.dp), contentAlignment = Alignment.Center) {
                    Text("hold a button", color = C.faint, fontSize = 11.sp)
                }
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            for (g in 1..6) GKey(g, Modifier.weight(1f))
        }
        Text("Keyboard: hold a key, or tap it to keep it on", color = C.muted, fontSize = 12.sp)
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            KeyToggle("Ctrl", NativeBridge.KEY_CTRL, Modifier.weight(1f))
            KeyToggle("Shift", NativeBridge.KEY_SHIFT, Modifier.weight(1f))
            KeyToggle("Alt", NativeBridge.KEY_ALT, Modifier.weight(1f))
        }
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            KeyToggle("Caps Lock", NativeBridge.KEY_CAPSLOCK, Modifier.weight(1f))
            KeyToggle("Num Lock", NativeBridge.KEY_NUMLOCK, Modifier.weight(1f))
            KeyToggle("Scroll Lock", NativeBridge.KEY_SCROLLLOCK, Modifier.weight(1f))
        }
        Row(horizontalArrangement = Arrangement.spacedBy(14.dp), verticalAlignment = Alignment.CenterVertically) {
            Legend(filled = true, "pressed")
            Legend(filled = false, "held by script")
        }
        Text(
            if (Bridge.device == NativeBridge.MAKCU)
                "Your real mouse buttons light up here (read through the MAKCU). Holding a button on screen does the same."
            else
                "Hold a button on screen to trigger the script, as if you pressed it on a Logitech mouse.",
            color = C.muted, fontSize = 12.sp, lineHeight = 16.sp,
        )
    }
}

@Composable
private fun PadButton(n: Int, label: String, mask: Int, modifier: Modifier, shape: Shape, bordered: Boolean = false) {
    val haptics = LocalHapticFeedback.current
    val physical = (mask and (1 shl (n - 1))) != 0
    val script = (mask and (1 shl (n - 1 + 8))) != 0
    val fill = when {
        physical -> C.accent.copy(alpha = 0.6f)
        script -> C.accent.copy(alpha = 0.18f)
        bordered -> C.field
        else -> Color.Transparent
    }
    Box(
        modifier
            .clip(shape)
            .background(fill)
            .border(
                if (script) 2.dp else 1.dp,
                if (script) C.accent else if (bordered) C.border else Color.Transparent,
                shape,
            )
            .pointerInput(n) {
                detectTapGestures(onPress = {
                    haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                    NativeBridge.touchButton(n, true)
                    try {
                        tryAwaitRelease()
                    } finally {
                        NativeBridge.touchButton(n, false)
                    }
                })
            },
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = if (physical) Color.White else C.muted, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
    }
}

@Composable
private fun GKey(n: Int, modifier: Modifier) {
    val haptics = LocalHapticFeedback.current
    var down by remember { mutableStateOf(false) }
    val shape = RoundedCornerShape(10.dp)
    Box(
        modifier
            .height(38.dp)
            .clip(shape)
            .background(if (down) C.accent.copy(alpha = 0.6f) else C.field)
            .border(1.dp, C.border, shape)
            .pointerInput(n) {
                detectTapGestures(onPress = {
                    haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                    down = true
                    NativeBridge.gKey(n, true)
                    try {
                        tryAwaitRelease()
                    } finally {
                        down = false
                        NativeBridge.gKey(n, false)
                    }
                })
            },
        contentAlignment = Alignment.Center,
    ) {
        Text("G$n", color = if (down) Color.White else C.muted, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
    }
}

/**
 * A key the script sees as held (IsModifierPressed) or switched on (IsKeyLockOn).
 * It's on while a finger is on it; a quick tap keeps it on until the next tap.
 */
@Composable
private fun KeyToggle(label: String, key: Int, modifier: Modifier) {
    val haptics = LocalHapticFeedback.current
    val on = (Bridge.keys and key) != 0
    val shape = RoundedCornerShape(10.dp)
    Box(
        modifier
            .height(38.dp)
            .clip(shape)
            .background(if (on) C.accent.copy(alpha = 0.6f) else C.field)
            .border(1.dp, if (on) C.accent else C.border, shape)
            .pointerInput(key) {
                detectTapGestures(onPress = {
                    haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                    val wasOn = (Bridge.keys and key) != 0
                    Bridge.setKey(key, true)  // held from the moment it's touched
                    val down = System.currentTimeMillis()
                    try {
                        tryAwaitRelease()
                    } finally {
                        val tap = System.currentTimeMillis() - down < 300
                        // tap: switch on (latched) or off; hold: only while held
                        if (wasOn || !tap) Bridge.setKey(key, false)
                    }
                })
            },
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = if (on) Color.White else C.muted, fontSize = 12.sp, fontWeight = FontWeight.SemiBold,
            maxLines = 1)
    }
}

@Composable
private fun Legend(filled: Boolean, text: String) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Box(
            Modifier
                .size(10.dp)
                .clip(CircleShape)
                .background(if (filled) C.accent else C.accent.copy(alpha = 0.18f))
                .border(1.5.dp, C.accent, CircleShape),
        )
        Text(text, color = C.muted, fontSize = 12.sp)
    }
}

// ------------------------------------------------------------------ options

@Composable
private fun OptionsCard() {
    val enabled = !Bridge.running
    SectionCard("Options") {
        Text("Randomize movement (px)", color = C.text, fontSize = 14.sp, fontWeight = FontWeight.Medium)
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            NumberField(Bridge.jitterMin, { Bridge.setJitter(it, Bridge.jitterMax) }, "Min", Modifier.weight(1f), enabled)
            NumberField(Bridge.jitterMax, { Bridge.setJitter(Bridge.jitterMin, it) }, "Max", Modifier.weight(1f), enabled)
        }
        Text(
            "Every move the script makes lands between Min and Max pixels off the exact path, for a hand-drawn " +
                "look. Max 0 = exact.",
            color = C.muted, fontSize = 12.sp, lineHeight = 16.sp,
        )
        Gap(2.dp)
        Text("PC screen size", color = C.text, fontSize = 14.sp, fontWeight = FontWeight.Medium)
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalAlignment = Alignment.CenterVertically) {
            NumberField(Bridge.screenWidth, { Bridge.setScreen(it, Bridge.screenHeight) }, "Width", Modifier.weight(1f),
                enabled, decimals = false)
            Text("×", color = C.muted, fontSize = 16.sp)
            NumberField(Bridge.screenHeight, { Bridge.setScreen(Bridge.screenWidth, it) }, "Height", Modifier.weight(1f),
                enabled, decimals = false)
        }
        Text(
            "For MoveMouseTo and GetMousePosition. The phone can't see the PC's pointer, so MoveMouseTo pushes it into " +
                "the top-left corner first. Turn off \"Enhance pointer precision\" in Windows for exact positions.",
            color = C.muted, fontSize = 12.sp, lineHeight = 16.sp,
        )
    }
}

// ------------------------------------------------------------------ log

@Composable
private fun LogCard() {
    val lines = Bridge.log.lines
    SectionCard(
        "Log",
        trailing = {
            TextButton(onClick = { Bridge.log.requestClear() }) { Text("Clear", color = C.muted, fontSize = 13.sp) }
        },
    ) {
        val state = rememberLazyListState()
        LaunchedEffect(lines.size) { if (lines.isNotEmpty()) state.scrollToItem(lines.size - 1) }
        val shape = RoundedCornerShape(12.dp)
        LazyColumn(
            state = state,
            modifier = Modifier
                .fillMaxWidth()
                .height(240.dp)
                .clip(shape)
                .background(C.logBg)
                .border(1.dp, C.cardBorder, shape)
                .padding(horizontal = 10.dp, vertical = 8.dp),
        ) {
            items(lines, key = { it.id }) { line ->
                Text(
                    line.text,
                    color = when (line.kind) {
                        LogKind.Script -> C.text
                        LogKind.App -> C.muted
                        LogKind.Error -> C.danger
                    },
                    fontFamily = FontFamily.Monospace, fontSize = 12.sp, lineHeight = 17.sp,
                )
            }
        }
    }
}

// ------------------------------------------------------------------ dialogs

@Composable
private fun ScriptsDialog(onDismiss: () -> Unit) {
    var confirmDelete by remember { mutableStateOf<String?>(null) }
    val version = Bridge.libraryVersion  // re-read the folder when it changes
    val mine = remember(version) { Bridge.scripts() }
    val examples = remember { Bridge.examples() }
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = { TextButton(onClick = onDismiss) { Text("Close") } },
        title = { Text("Scripts") },
        text = {
            LazyColumn(Modifier.height(380.dp)) {
                item { Caption("Your scripts", Modifier.padding(vertical = 8.dp)) }
                if (mine.isEmpty()) item { Text("None yet. Open a .lua file or make a new one.", color = C.muted, fontSize = 13.sp) }
                items(mine) { name ->
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(10.dp))
                            .clickable { Bridge.select(name); onDismiss() }
                            .padding(start = 10.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(name, color = if (name == Bridge.script) C.accent else C.text, modifier = Modifier.weight(1f),
                            maxLines = 1, overflow = TextOverflow.Ellipsis)
                        IconButton(onClick = { confirmDelete = name }) {
                            Icon(Icons.Rounded.Delete, "Delete", tint = C.faint)
                        }
                    }
                }
                item { Caption("Examples", Modifier.padding(top = 16.dp, bottom = 8.dp)) }
                items(examples) { name ->
                    Text(
                        name, color = C.text,
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(10.dp))
                            .clickable { Bridge.useExample(name); onDismiss() }
                            .padding(10.dp),
                    )
                }
            }
        },
    )
    confirmDelete?.let { name ->
        AlertDialog(
            onDismissRequest = { confirmDelete = null },
            title = { Text("Delete $name?") },
            text = { Text("The script is removed from this phone.") },
            confirmButton = {
                TextButton(onClick = { Bridge.delete(name); confirmDelete = null }) { Text("Delete", color = C.danger) }
            },
            dismissButton = { TextButton(onClick = { confirmDelete = null }) { Text("Cancel") } },
        )
    }
}

@Composable
private fun NewScriptDialog(onDismiss: () -> Unit, onCreated: (String) -> Unit) {
    var name by remember { mutableStateOf("my_script") }
    var error by remember { mutableStateOf<String?>(null) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("New script") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(
                    value = name, onValueChange = { name = it; error = null }, singleLine = true,
                    label = { Text("Name") }, suffix = { Text(".lua") }, isError = error != null,
                )
                error?.let { Text(it, color = C.danger, fontSize = 12.sp) }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                val created = Bridge.create(name)
                if (created == null) error = "${Bridge.fileName(name)} already exists." else onCreated(created)
            }) { Text("Create") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
