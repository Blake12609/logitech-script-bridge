package io.github.blake12609.scriptbridge

import android.app.Application
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.SharedPreferences
import android.hardware.usb.UsbManager
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.ContextCompat
import java.io.File
import java.util.concurrent.Executors

/** App-wide state: settings, the USB link, the script library and the running script. */
object Bridge {
    enum class Link { Off, Waiting, Connected }

    class DeviceInfo(val id: Int, val label: String, val serial: Boolean, val hint: String)

    val devices = listOf(
        DeviceInfo(
            NativeBridge.MAKCU, "MAKCU", true,
            "Plug the MAKCU's COM port into this phone with a USB OTG adapter. Its other port stays in the PC " +
                "and your mouse in the MAKCU, so your real mouse buttons trigger the script.",
        ),
        DeviceInfo(
            NativeBridge.KMBOX, "KMBox B / B+ / B Pro", true,
            "Mouse only. Trigger the script with the on-screen buttons below.",
        ),
        DeviceInfo(
            NativeBridge.ESP32, "ESP32-S3 (bridge firmware)", true,
            "Mouse and keyboard. Connect the board's UART (COM) port to this phone; its native USB port goes to the PC.",
        ),
        DeviceInfo(
            NativeBridge.DEMO, "Demo (no hardware)", false,
            "Nothing is sent. Every click, move and key the script makes is written to the log.",
        ),
    )
    val baudRates = listOf(9600, 57600, 115200, 230400, 460800, 921600, 4000000)

    lateinit var usb: UsbLink
        private set
    val log = LogBuffer()

    private lateinit var app: Application
    private lateinit var prefs: SharedPreferences
    private val main = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor()  // starting/stopping can block briefly

    var device by mutableIntStateOf(NativeBridge.MAKCU)
        private set
    var baud by mutableIntStateOf(115200)
        private set
    var jitterMin by mutableStateOf("0")
        private set
    var jitterMax by mutableStateOf("0")
        private set
    var screenWidth by mutableStateOf("1920")
        private set
    var screenHeight by mutableStateOf("1080")
        private set

    var link by mutableStateOf(Link.Off)
        private set
    var linkText by mutableStateOf("Not connected")
        private set
    var running by mutableStateOf(false)
        private set
    var busy by mutableStateOf(false)
        private set
    var script by mutableStateOf<String?>(null)
        private set
    /** Line of the last load error in [script], 0 if none. */
    var errorLine by mutableIntStateOf(0)
        private set
    /** Changes whenever the script library does. */
    var libraryVersion by mutableIntStateOf(0)
        private set

    val deviceInfo: DeviceInfo get() = devices.first { it.id == device }

    fun init(application: Application) {
        app = application
        prefs = app.getSharedPreferences("bridge", Context.MODE_PRIVATE)
        device = prefs.getInt("device", NativeBridge.MAKCU).takeIf { d -> devices.any { it.id == d } } ?: NativeBridge.MAKCU
        baud = prefs.getInt("baud", 115200)
        jitterMin = prefs.getString("jitter_min", "0") ?: "0"
        jitterMax = prefs.getString("jitter_max", "0") ?: "0"
        screenWidth = prefs.getString("screen_w", "1920") ?: "1920"
        screenHeight = prefs.getString("screen_h", "1080") ?: "1080"
        script = prefs.getString("script", null)?.takeIf { File(scriptsDir, it).isFile }
        if (script == null && !prefs.getBoolean("seeded", false)) {
            // first run: start with an example so there's something to try
            prefs.edit().putBoolean("seeded", true).apply()
            examples().firstOrNull { it.startsWith("hold_to_autoclick") }?.let { useExample(it) }
        }

        usb = UsbLink(app)
        usb.listener = object : UsbLink.Listener {
            override fun onConnected(description: String) = main.post0 {
                link = Link.Connected
                linkText = description
                log.app("Connected: $description")
            }

            override fun onWaitingForPermission(description: String) = main.post0 {
                link = Link.Waiting
                linkText = "Allow USB access to $description"
            }

            override fun onDisconnected(reason: String?) = main.post0 {
                val wasConnected = link == Link.Connected
                link = Link.Off
                linkText = "Not connected"
                if (reason != null) log.error(reason) else if (wasConnected) log.app("Disconnected.")
                if (running && device != NativeBridge.DEMO) stop()
            }
        }
        ContextCompat.registerReceiver(
            app, usbReceiver,
            IntentFilter().apply {
                addAction(UsbLink.ACTION_PERMISSION)
                addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
                addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
            },
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )
        log.app("Logitech Script Bridge ${BuildConfig.VERSION_NAME} for Android")
    }

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                UsbLink.ACTION_PERMISSION ->
                    usb.onPermissionResult(intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false))
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> autoConnect()
                UsbManager.ACTION_USB_DEVICE_DETACHED -> if (usb.isOpen && usb.findDriver() == null) usb.disconnect()
            }
        }
    }

    // ------------------------------------------------------------ settings

    fun selectDevice(id: Int) {
        if (running || id == device) return
        device = id
        prefs.edit().putInt("device", id).apply()
        if (usb.isOpen) {
            NativeBridge.setDevice(id)
            usb.write(NativeBridge.connectCommands(id))
        }
    }

    fun selectBaud(rate: Int) {
        baud = rate
        prefs.edit().putInt("baud", rate).apply()
    }

    fun setJitter(min: String, max: String) {
        jitterMin = min
        jitterMax = max
        prefs.edit().putString("jitter_min", min).putString("jitter_max", max).apply()
    }

    fun setScreen(width: String, height: String) {
        screenWidth = width
        screenHeight = height
        prefs.edit().putString("screen_w", width).putString("screen_h", height).apply()
    }

    private fun number(s: String) = s.trim().replace(',', '.').toDoubleOrNull() ?: 0.0

    // ------------------------------------------------------------ connection

    fun connect() {
        if (link == Link.Off) usb.connect(baud, device)
    }

    fun disconnect() = usb.disconnect()

    /** A USB device was plugged in, or the app was opened because of one. */
    fun autoConnect() {
        if (device != NativeBridge.DEMO && link == Link.Off && usb.findDriver() != null) connect()
    }

    // ------------------------------------------------------------ script library

    val scriptsDir: File get() = File(app.filesDir, "scripts").also { it.mkdirs() }

    fun scripts(): List<String> =
        scriptsDir.listFiles()?.filter { it.isFile }?.map { it.name }?.sortedBy { it.lowercase() } ?: emptyList()

    fun examples(): List<String> = app.assets.list("")?.filter { it.endsWith(".lua") }?.sorted() ?: emptyList()

    fun readScript(name: String): String = runCatching { File(scriptsDir, name).readText() }.getOrDefault("")

    fun select(name: String?) {
        if (running) return
        script = name
        errorLine = 0
        prefs.edit().putString("script", name).apply()
    }

    /** Saves a script; a running script restarts with the new code. */
    fun save(name: String, text: String): Boolean {
        val ok = runCatching { File(scriptsDir, name).writeText(text) }.isSuccess
        if (!ok) {
            log.error("Cannot save $name")
            return false
        }
        libraryVersion++
        if (name == script) errorLine = 0
        log.app("Saved $name")
        if (running && name == script) restart()
        return true
    }

    /** Copies an example into the library (once) and selects it. */
    fun useExample(name: String) {
        val file = File(scriptsDir, name)
        if (!file.exists()) runCatching { app.assets.open(name).use { file.writeBytes(it.readBytes()) } }
        libraryVersion++
        select(name)
    }

    /** Imports a .lua file picked in the system file picker. */
    fun import(uri: Uri): String? {
        val display = app.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) it.getString(0) else null
        } ?: "script.lua"
        val name = fileName(display)
        val bytes = runCatching { app.contentResolver.openInputStream(uri)?.use { it.readBytes() } }.getOrNull()
        if (bytes == null) {
            log.error("Cannot read $display")
            return null
        }
        File(scriptsDir, name).writeBytes(bytes)
        libraryVersion++
        select(name)
        log.app("Imported $name")
        return name
    }

    /** Creates a script from the template; null if the name is taken. */
    fun create(rawName: String): String? {
        val name = fileName(rawName)
        val file = File(scriptsDir, name)
        if (file.exists()) return null
        file.writeText(TEMPLATE)
        libraryVersion++
        select(name)
        return name
    }

    fun delete(name: String) {
        if (running && name == script) return
        File(scriptsDir, name).delete()
        if (name == script) select(null)
        libraryVersion++
    }

    fun fileName(raw: String): String {
        var base = raw.trim().substringAfterLast('/').replace(Regex("[^A-Za-z0-9 _.()-]"), "_")
        if (base.endsWith(".lua", ignoreCase = true)) base = base.dropLast(4)
        if (base.isBlank()) base = "script"
        return "$base.lua"
    }

    // ------------------------------------------------------------ running

    /** Starts the selected script. Returns a message for the user if it can't. */
    fun start(): String? {
        if (running || busy) return null
        val name = script ?: return "Pick or write a script first."
        if (device != NativeBridge.DEMO && link != Link.Connected)
            return "Connect the ${deviceInfo.label} first (or pick Demo to try without hardware)."
        val text = readScript(name)
        val dev = device
        val jMin = number(jitterMin)
        val jMax = number(jitterMax)
        val w = screenWidth.trim().toIntOrNull()?.coerceIn(1, 32767) ?: 1920
        val h = screenHeight.trim().toIntOrNull()?.coerceIn(1, 32767) ?: 1080
        busy = true
        worker.execute {
            val err = NativeBridge.start(text.toByteArray(), name.toByteArray(), dev, jMin, jMax, w, h)
            main.post {
                busy = false
                if (err == null) {
                    running = true
                    errorLine = 0
                    ContextCompat.startForegroundService(app, Intent(app, BridgeService::class.java))
                } else {
                    errorLine = Regex(":(\\d+):").find(err)?.groupValues?.get(1)?.toIntOrNull() ?: 0
                    log.error(err)
                }
            }
        }
        return null
    }

    fun stop() {
        if (!running || busy) return
        busy = true
        worker.execute {
            NativeBridge.stop()
            main.post {
                busy = false
                running = false
                app.stopService(Intent(app, BridgeService::class.java))
            }
        }
    }

    private fun restart() {
        if (busy) return
        busy = true
        worker.execute {
            NativeBridge.stop()
            main.post {
                busy = false
                running = false
                start()
            }
        }
    }

    /** Called by the UI a few times a second. */
    fun tick() {
        log.drain()
        if (running && !busy && !NativeBridge.isRunning()) {
            running = false
            app.stopService(Intent(app, BridgeService::class.java))
        }
    }

    private inline fun Handler.post0(crossinline block: () -> Unit) {
        post { block() }
    }

    private const val TEMPLATE = """-- New script. Save it, then press Start.
function OnEvent(event, arg, family)
    if event == "PROFILE_ACTIVATED" then
        OutputLogMessage("Script started\n")
    end

    if event == "MOUSE_BUTTON_PRESSED" and arg == 4 then
        OutputLogMessage("Back button pressed\n")
    end
end
"""
}
