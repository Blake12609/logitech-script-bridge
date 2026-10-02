package io.github.blake12609.scriptbridge

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import com.hoho.android.usbserial.driver.CdcAcmSerialDriver
import com.hoho.android.usbserial.driver.ProbeTable
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber
import com.hoho.android.usbserial.util.SerialInputOutputManager

/**
 * The USB OTG serial port to the MAKCU (or other device). Everything it reports
 * goes through [listener]; callbacks can come from any thread.
 */
class UsbLink(private val context: Context) {
    interface Listener {
        fun onConnected(description: String)
        fun onWaitingForPermission(description: String)
        fun onDisconnected(reason: String?)
    }

    var listener: Listener? = null

    private val usb = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private val lock = Any()
    private var port: UsbSerialPort? = null
    @Volatile private var io: SerialInputOutputManager? = null
    private var pendingBaud = 115200
    private var pendingDevice = NativeBridge.MAKCU

    // Chips the stock prober might not know; they all speak standard CDC-ACM.
    private val extraProber = UsbSerialProber(
        ProbeTable().apply {
            addProduct(0x1A86, 0x55D3, CdcAcmSerialDriver::class.java)  // CH343 (MAKCU)
            addProduct(0x1A86, 0x55D4, CdcAcmSerialDriver::class.java)  // CH9102
            addProduct(0x303A, 0x1001, CdcAcmSerialDriver::class.java)  // ESP32-S3 USB serial
        },
    )

    val isOpen: Boolean get() = synchronized(lock) { port != null }

    fun findDriver(): UsbSerialDriver? {
        for (device in usb.deviceList.values) {
            val driver = UsbSerialProber.getDefaultProber().probeDevice(device) ?: extraProber.probeDevice(device)
            if (driver != null && driver.ports.isNotEmpty()) return driver
        }
        return null
    }

    /** Opens the first USB serial device, asking for permission first if needed. */
    fun connect(baud: Int, device: Int) {
        if (isOpen) return
        pendingBaud = baud
        pendingDevice = device
        val driver = findDriver()
        if (driver == null) {
            listener?.onDisconnected("No USB serial device found. Plug the device into the phone (USB OTG).")
            return
        }
        if (!usb.hasPermission(driver.device)) {
            val flags = if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0
            val intent = Intent(ACTION_PERMISSION).setPackage(context.packageName)
            usb.requestPermission(driver.device, PendingIntent.getBroadcast(context, 0, intent, flags))
            listener?.onWaitingForPermission(describe(driver.device))
            return
        }
        open(driver)
    }

    /** The answer to the permission dialog from [connect]. */
    fun onPermissionResult(granted: Boolean) {
        if (!granted) {
            listener?.onDisconnected("USB permission was denied.")
            return
        }
        findDriver()?.let { if (!isOpen) open(it) }
    }

    private fun open(driver: UsbSerialDriver) {
        val connection = usb.openDevice(driver.device)
        if (connection == null) {
            listener?.onDisconnected("Cannot open ${describe(driver.device)}.")
            return
        }
        val p = driver.ports[0]
        try {
            p.open(connection)
            if (pendingDevice == NativeBridge.MAKCU) {
                // Like MAKCU's own library: ask for 4 Mbaud at the power-on speed, then switch.
                // A MAKCU that is already at 4 Mbaud just ignores the request.
                p.setParameters(115200, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
                p.write(MAKCU_4M, WRITE_TIMEOUT_MS)
                Thread.sleep(30)
                pendingBaud = 4000000
            }
            p.setParameters(pendingBaud, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
        } catch (e: Exception) {
            runCatching { p.close() }
            listener?.onDisconnected("Cannot open ${describe(driver.device)}: ${e.message}")
            return
        }
        synchronized(lock) { port = p }
        io = SerialInputOutputManager(p, object : SerialInputOutputManager.Listener {
            override fun onNewData(data: ByteArray) = NativeBridge.onSerialData(data)
            override fun onRunError(e: Exception) {
                if (close()) listener?.onDisconnected("The device was disconnected.")
            }
        }).also { it.start() }
        NativeBridge.setDevice(pendingDevice)
        write(NativeBridge.connectCommands(pendingDevice))
        listener?.onConnected("${describe(driver.device)} at $pendingBaud baud")
    }

    /** Sends raw bytes; called from the engine's thread. */
    fun write(bytes: ByteArray) {
        if (bytes.isEmpty()) return
        val failed = synchronized(lock) {
            val p = port ?: return
            try {
                p.write(bytes, WRITE_TIMEOUT_MS)
                false
            } catch (e: Exception) {
                true
            }
        }
        if (failed && close()) listener?.onDisconnected("Lost the connection to the device.")
    }

    fun disconnect() {
        if (close()) listener?.onDisconnected(null)
    }

    /** Closes the port; false if it wasn't open. */
    private fun close(): Boolean {
        val p = synchronized(lock) {
            val p = port ?: return false
            port = null
            p
        }
        io?.stop()
        io = null
        runCatching { p.close() }
        return true
    }

    /** Whether a device that was just plugged in is one we can talk to. */
    fun isSerialDevice(device: UsbDevice) =
        UsbSerialProber.getDefaultProber().probeDevice(device) != null || extraProber.probeDevice(device) != null

    private fun describe(d: UsbDevice): String {
        val name = d.productName?.takeIf { it.isNotBlank() } ?: chipName(d)
        return "$name (%04X:%04X)".format(d.vendorId, d.productId)
    }

    private fun chipName(d: UsbDevice) = when (d.vendorId) {
        0x1A86 -> if (d.productId == 0x55D3) "CH343" else "CH34x"
        0x10C4 -> "CP210x"
        0x0403 -> "FTDI"
        0x303A -> "ESP32"
        else -> "USB serial"
    }

    companion object {
        const val ACTION_PERMISSION = "io.github.blake12609.scriptbridge.USB_PERMISSION"
        private const val WRITE_TIMEOUT_MS = 500
        private val MAKCU_4M = byteArrayOf(0xDE.toByte(), 0xAD.toByte(), 0x05, 0x00, 0xA5.toByte(), 0x00, 0x09, 0x3D, 0x00)
    }
}
