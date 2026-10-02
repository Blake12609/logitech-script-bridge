// Stand-in for android/.../NativeBridge.kt with the same JNI signatures, so jni_bridge.cpp
// can be loaded and driven on a PC (run by ctest as jni_tests).
package io.github.blake12609.scriptbridge;
import java.nio.charset.StandardCharsets;
public class NativeBridge {
    static { System.loadLibrary("scriptbridge"); }
    static final StringBuffer written = new StringBuffer(), logged = new StringBuffer();
    static volatile int clears = 0;
    public static native String start(byte[] script, byte[] name, int device, double jMin, double jMax, int w, int h);
    public static native void stop();
    public static native boolean isRunning();
    public static native void setDevice(int d);
    public static native byte[] connectCommands(int d);
    public static native void onSerialData(byte[] data);
    public static native void touchButton(int n, boolean pressed);
    public static native void gKey(int n, boolean pressed);
    public static native int buttonMask();
    public static native byte[] checkSyntax(byte[] script, byte[] name);
    public static void onWrite(byte[] b) { written.append(new String(b, StandardCharsets.UTF_8)); }
    public static void onLog(byte[] b) { logged.append(new String(b, StandardCharsets.UTF_8)); }
    public static void onClearLog() { clears++; }
    static byte[] u(String s) { return s.getBytes(StandardCharsets.UTF_8); }
    static void check(boolean c, String what) { if (!c) { System.out.println("FAIL " + what); System.exit(1); } System.out.println("ok " + what); }
    public static void main(String[] a) throws Exception {
        check(new String(connectCommands(0), "UTF-8").equals("km.buttons(1)\r\n"), "connect commands");
        check(new String(checkSyntax(u("x = = 1"), u("t.lua")), "UTF-8").startsWith("t.lua:1:"), "syntax error");
        check(checkSyntax(u("x = 1"), u("t.lua")).length == 0, "syntax ok");
        String err = start(u("function OnEvent("), u("bad.lua"), 3, 0, 0, 1920, 1080);
        check(err != null && err.startsWith("bad.lua:1:"), "load error " + err);
        String script = "function OnEvent(e, a)\n OutputLogMessage(\"ev %s %d \\u{2713}\\n\", e, a)\n" +
            " if e == \"MOUSE_BUTTON_PRESSED\" and a == 4 then PressAndReleaseMouseButton(1) ClearLog() end\nend";
        check(start(u(script), u("ok.lua"), 0, 0, 0, 1920, 1080) == null, "start");
        check(isRunning(), "running");
        onSerialData(new byte[]{0x08});
        Thread.sleep(150);
        check((buttonMask() & 0x08) != 0, "mask from stream");
        onSerialData(new byte[]{0x00});
        touchButton(4, true); touchButton(4, false); gKey(2, true);
        Thread.sleep(150);
        stop();
        check(!isRunning(), "stopped");
        System.out.println("written=" + written.toString().replace("\r\n", "|"));
        System.out.println("log=" + logged);
        check(written.toString().equals("km.left(1)\r\nkm.left(0)\r\nkm.left(1)\r\nkm.left(0)\r\n"), "writes");
        check(logged.toString().contains("ev G_PRESSED 2 ✓"), "utf8 log");
        check(clears == 2, "clear log callbacks");
    }
}
