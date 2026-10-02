package io.github.blake12609.scriptbridge

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import io.github.blake12609.scriptbridge.ui.EditorScreen
import io.github.blake12609.scriptbridge.ui.MainScreen
import io.github.blake12609.scriptbridge.ui.ScriptBridgeTheme
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {
    private val notificationPermission = registerForActivityResult(ActivityResultContracts.RequestPermission()) {}

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        handleUsbIntent(intent)
        if (savedInstanceState == null) Bridge.autoConnect()
        setContent { ScriptBridgeTheme { App(::startScript) } }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleUsbIntent(intent)
    }

    private fun handleUsbIntent(intent: Intent?) {
        if (intent?.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) Bridge.autoConnect()
    }

    private fun startScript() {
        // the "script running" notification needs permission on Android 13+
        if (Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
        Bridge.start()?.let { Toast.makeText(this, it, Toast.LENGTH_LONG).show() }
    }
}

@Composable
private fun App(startScript: () -> Unit) {
    var editing by rememberSaveable { mutableStateOf<String?>(null) }
    var mask by remember { mutableIntStateOf(0) }
    LaunchedEffect(Unit) {
        while (true) {
            Bridge.tick()
            mask = NativeBridge.buttonMask()
            delay(60)
        }
    }
    val name = editing
    if (name == null) {
        MainScreen(
            mask = mask,
            onStart = { if (Bridge.running) Bridge.stop() else startScript() },
            onEdit = { editing = it },
        )
    } else {
        EditorScreen(name, onClose = { editing = null })
    }
}
