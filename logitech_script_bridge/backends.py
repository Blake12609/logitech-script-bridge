"""Output devices: where the mouse/keyboard actions of a script end up.

Every backend implements the same tiny interface:

    move(dx, dy)            relative mouse move in counts
    wheel(n)                scroll wheel clicks (positive = up)
    button(name, down)      name is one of BUTTONS
    key(hid, down)          USB HID usage ID (only if supports_keyboard)
"""

import threading
import time

BUTTONS = ("left", "right", "middle", "side1", "side2")


class Backend:
    name = "backend"
    supports_keyboard = False

    def open(self):
        pass

    def close(self):
        pass

    def move(self, dx, dy):
        raise NotImplementedError

    def wheel(self, n):
        raise NotImplementedError

    def button(self, name, down):
        raise NotImplementedError

    def key(self, hid, down):
        raise NotImplementedError


class DryRunBackend(Backend):
    """Does nothing but record/log what would have been sent. Good for testing scripts."""

    name = "dry-run"
    supports_keyboard = True

    def __init__(self, log=None):
        self.log = log
        self.sent = []

    def _emit(self, *cmd):
        self.sent.append(cmd)
        if self.log:
            self.log("[dry-run] %s\n" % " ".join(str(c) for c in cmd))

    def move(self, dx, dy):
        self._emit("move", dx, dy)

    def wheel(self, n):
        self._emit("wheel", n)

    def button(self, name, down):
        self._emit("button", name, int(down))

    def key(self, hid, down):
        self._emit("key", "0x%02X" % hid, int(down))


class SerialBackend(Backend):
    """MAKCU / ESP32-S3 (bridge firmware) over a serial port.

    Both speak the MAKCU text protocol for the mouse:
        km.move(x,y)  km.wheel(n)  km.left(1|0)  km.right  km.middle  km.side1  km.side2
    The bridge firmware in firmware/esp32s3_bridge additionally understands
        kb.down(hid)  kb.up(hid)
    so it can type keys too. Stock MAKCU firmware is mouse-only.
    """

    name = "serial"
    # Magic sequence understood by MAKCU to switch its serial link to 4 Mbaud.
    MAKCU_4M = bytes([0xDE, 0xAD, 0x05, 0x00, 0xA5, 0x00, 0x09, 0x3D, 0x00])

    def __init__(self, port, baud=115200, keyboard=False, makcu_high_speed=False, log=None):
        self.port = port
        self.baud = baud
        self.supports_keyboard = keyboard
        self.makcu_high_speed = makcu_high_speed
        self.log = log
        self._ser = None
        self._lock = threading.Lock()
        self._reader = None

    def open(self):
        import serial  # pyserial

        self._ser = serial.Serial(self.port, self.baud, timeout=0.05, write_timeout=1)
        if self.makcu_high_speed:
            self._ser.write(self.MAKCU_4M)
            self._ser.flush()
            time.sleep(0.05)
            self._ser.baudrate = 4000000
        # Devices echo commands back; drain them so the OS buffer never fills up.
        self._reader = threading.Thread(target=self._drain, daemon=True)
        self._reader.start()

    def _drain(self):
        while self._ser is not None:
            try:
                self._ser.read(self._ser.in_waiting or 1)
            except Exception:
                break

    def close(self):
        ser, self._ser = self._ser, None
        if ser is not None:
            ser.close()

    def _send(self, cmd):
        with self._lock:
            if self._ser is None:
                raise RuntimeError("serial port %s is not open" % self.port)
            self._ser.write((cmd + "\r\n").encode("ascii"))

    def move(self, dx, dy):
        if dx or dy:
            self._send("km.move(%d,%d)" % (dx, dy))

    def wheel(self, n):
        if n:
            self._send("km.wheel(%d)" % n)

    def button(self, name, down):
        self._send("km.%s(%d)" % (name, 1 if down else 0))

    def key(self, hid, down):
        self._send("kb.%s(%d)" % ("down" if down else "up", hid))


class SoftwareBackend(Backend):
    """Plain OS-level input injection (pynput). No hardware needed, handy for testing.

    Note: many games ignore or block injected input, which is why a hardware
    device is the main target of this app.
    """

    name = "software"
    supports_keyboard = True

    def open(self):
        from pynput import keyboard, mouse

        self._kb_mod = keyboard
        self._mouse = mouse.Controller()
        self._kb = keyboard.Controller()
        self._buttons = {
            "left": mouse.Button.left,
            "right": mouse.Button.right,
            "middle": mouse.Button.middle,
            "side1": getattr(mouse.Button, "x1", None),
            "side2": getattr(mouse.Button, "x2", None),
        }

    def move(self, dx, dy):
        self._mouse.move(dx, dy)

    def wheel(self, n):
        self._mouse.scroll(0, n)

    def button(self, name, down):
        btn = self._buttons.get(name)
        if btn is None:
            return
        (self._mouse.press if down else self._mouse.release)(btn)

    def key(self, hid, down):
        k = pynput_key(self._kb_mod, hid)
        if k is not None:
            (self._kb.press if down else self._kb.release)(k)


_PYNPUT_SPECIAL = {
    "escape": "esc", "backspace": "backspace", "tab": "tab", "enter": "enter",
    "lctrl": "ctrl_l", "rctrl": "ctrl_r", "lshift": "shift_l", "rshift": "shift_r",
    "lalt": "alt_l", "ralt": "alt_r", "lgui": "cmd_l", "rgui": "cmd_r",
    "spacebar": "space", "capslock": "caps_lock", "numlock": "num_lock",
    "scrolllock": "scroll_lock", "printscreen": "print_screen", "pause": "pause",
    "insert": "insert", "delete": "delete", "home": "home", "end": "end",
    "pageup": "page_up", "pagedown": "page_down", "up": "up", "down": "down",
    "left": "left", "right": "right", "appkey": "menu", "numenter": "enter",
}
_PYNPUT_CHARS = {
    "minus": "-", "equal": "=", "lbracket": "[", "rbracket": "]", "backslash": "\\",
    "semicolon": ";", "quote": "'", "tilde": "`", "comma": ",", "period": ".",
    "slash": "/", "numstar": "*", "numminus": "-", "numplus": "+", "numslash": "/",
    "numperiod": ".",
}


def pynput_key(kb, hid):
    from .keys import HID_TO_NAME

    name = HID_TO_NAME.get(hid)
    if name is None:
        return None
    if name in _PYNPUT_SPECIAL:
        return getattr(kb.Key, _PYNPUT_SPECIAL[name], None)
    if name.startswith("f") and name[1:].isdigit():
        return getattr(kb.Key, name, None)
    if name.startswith("num") and name[3:].isdigit():
        return kb.KeyCode.from_char(name[3:])
    if name in _PYNPUT_CHARS:
        return kb.KeyCode.from_char(_PYNPUT_CHARS[name])
    if len(name) == 1:
        return kb.KeyCode.from_char(name)
    return None


DEVICES = {
    "makcu": "MAKCU",
    "esp32": "ESP32-S3 (bridge firmware)",
    "software": "Software (no hardware)",
    "dry-run": "Dry run (log only)",
}


def create_backend(device, port=None, baud=115200, log=None, makcu_high_speed=False):
    if device == "makcu":
        return SerialBackend(port, baud, keyboard=False, makcu_high_speed=makcu_high_speed, log=log)
    if device == "esp32":
        return SerialBackend(port, baud, keyboard=True, log=log)
    if device == "software":
        return SoftwareBackend()
    if device == "dry-run":
        return DryRunBackend(log=log)
    raise ValueError("unknown device %r (choose from %s)" % (device, ", ".join(DEVICES)))


def list_serial_ports():
    try:
        from serial.tools import list_ports
    except ImportError:
        return []
    return [(p.device, p.description) for p in list_ports.comports()]
