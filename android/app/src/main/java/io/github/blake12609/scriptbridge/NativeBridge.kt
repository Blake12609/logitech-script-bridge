package io.github.blake12609.scriptbridge

/** The C++ engine (src/mobile_session.cpp), reached through cpp/jni_bridge.cpp. */
object NativeBridge {
    init {
        System.loadLibrary("scriptbridge")
    }

    // Device numbers, shared with MobileDevice in mobile_session.h.
    const val MAKCU = 0
    const val KMBOX = 1
    const val ESP32 = 2
    const val DEMO = 3

    /** Returns null when the script started, otherwise the error. */
    @JvmStatic external fun start(
        script: ByteArray, name: ByteArray, device: Int,
        jitterMin: Double, jitterMax: Double, screenWidth: Int, screenHeight: Int,
    ): String?

    @JvmStatic external fun stop()
    @JvmStatic external fun isRunning(): Boolean
    @JvmStatic external fun setDevice(device: Int)
    @JvmStatic external fun connectCommands(device: Int): ByteArray
    @JvmStatic external fun onSerialData(data: ByteArray)
    @JvmStatic external fun touchButton(n: Int, pressed: Boolean)
    @JvmStatic external fun gKey(n: Int, pressed: Boolean)

    /** On-screen keys for IsModifierPressed / IsKeyLockOn, bits as in MobileSession::Key. */
    @JvmStatic external fun setKeys(mask: Int)
    const val KEY_CTRL = 1
    const val KEY_SHIFT = 2
    const val KEY_ALT = 4
    const val KEY_CAPSLOCK = 8
    const val KEY_NUMLOCK = 16
    const val KEY_SCROLLLOCK = 32

    /** Bits 0-4: buttons 1-5 held on the mouse or screen. Bits 8-12: held by the script. */
    @JvmStatic external fun buttonMask(): Int

    /** "" when the script compiles, otherwise "name:line: problem". */
    @JvmStatic external fun checkSyntax(script: ByteArray, name: ByteArray): ByteArray

    // Called from C++, on the engine's threads.
    // They must return void: jni_bridge.cpp looks them up as ([B)V and ()V.
    @JvmStatic fun onWrite(bytes: ByteArray) {
        Bridge.usb.write(bytes)
    }

    @JvmStatic fun onLog(bytes: ByteArray) {
        Bridge.log.add(String(bytes, Charsets.UTF_8))
    }

    @JvmStatic fun onClearLog() {
        Bridge.log.requestClear()
    }
}
