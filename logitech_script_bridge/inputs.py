"""Reading the real mouse/keyboard: button events, modifier/lock state, cursor position."""

import sys

IS_WINDOWS = sys.platform == "win32"

# Logitech OnEvent numbering for mouse buttons.
EVENT_BUTTON = {"left": 1, "right": 2, "middle": 3, "side1": 4, "side2": 5}

_VK_MODIFIERS = {
    "lshift": 0xA0, "rshift": 0xA1, "lctrl": 0xA2, "rctrl": 0xA3, "lalt": 0xA4, "ralt": 0xA5,
}
_VK_LOCKS = {"capslock": 0x14, "numlock": 0x90, "scrolllock": 0x91}


class InputMonitor:
    """Watches the physical mouse through pynput and reports button presses.

    on_button(name, pressed) is called from a background thread, where name is
    one of backends.BUTTONS.
    """

    def __init__(self, on_button):
        self.on_button = on_button
        self._mods = set()
        self._locks = set()
        self._listeners = []
        self._mouse = None
        self._rect = None

    def start(self):
        from pynput import keyboard, mouse

        names = {
            mouse.Button.left: "left",
            mouse.Button.right: "right",
            mouse.Button.middle: "middle",
        }
        for attr, name in (("x1", "side1"), ("x2", "side2"), ("button8", "side1"), ("button9", "side2")):
            if hasattr(mouse.Button, attr):
                names[getattr(mouse.Button, attr)] = name

        def on_click(x, y, button, pressed, *rest):
            name = names.get(button)
            if name:
                self.on_button(name, pressed)

        K = keyboard.Key
        mod_keys = {
            K.shift_l: "lshift", K.shift: "lshift", K.shift_r: "rshift",
            K.ctrl_l: "lctrl", K.ctrl: "lctrl", K.ctrl_r: "rctrl",
            K.alt_l: "lalt", K.alt: "lalt", K.alt_r: "ralt",
        }
        lock_keys = {K.caps_lock: "capslock"}
        for attr, name in (("num_lock", "numlock"), ("scroll_lock", "scrolllock")):
            if hasattr(K, attr):
                lock_keys[getattr(K, attr)] = name

        def on_press(key, *rest):
            if key in mod_keys:
                self._mods.add(mod_keys[key])
            elif key in lock_keys:
                self._locks ^= {lock_keys[key]}

        def on_release(key, *rest):
            self._mods.discard(mod_keys.get(key))

        self._mouse = mouse.Controller()
        self._listeners = [
            mouse.Listener(on_click=on_click),
            keyboard.Listener(on_press=on_press, on_release=on_release),
        ]
        for listener in self._listeners:
            listener.daemon = True
            listener.start()

    def stop(self):
        for listener in self._listeners:
            listener.stop()
        self._listeners = []

    def modifier_pressed(self, name):
        if IS_WINDOWS:
            import ctypes

            return bool(ctypes.windll.user32.GetAsyncKeyState(_VK_MODIFIERS[name]) & 0x8000)
        return name in self._mods

    def lock_on(self, name):
        if IS_WINDOWS:
            import ctypes

            return bool(ctypes.windll.user32.GetKeyState(_VK_LOCKS[name]) & 1)
        return name in self._locks

    def cursor_position(self):
        if self._mouse is None:
            return (0, 0)
        x, y = self._mouse.position
        return (int(x), int(y))

    def screen_rect(self, virtual=False):
        """(left, top, width, height) of the primary screen or the whole virtual desktop."""
        if IS_WINDOWS:
            import ctypes

            m = ctypes.windll.user32.GetSystemMetrics
            if virtual:
                return (m(76), m(77), m(78), m(79))
            return (0, 0, m(0), m(1))
        if self._rect is None:
            self._rect = self._query_screen()
        return self._rect

    @staticmethod
    def _query_screen():
        try:
            from Xlib import display  # installed alongside pynput on Linux

            d = display.Display()
            screen = d.screen()
            rect = (0, 0, screen.width_in_pixels, screen.height_in_pixels)
            d.close()
            return rect
        except Exception:
            pass
        try:
            import tkinter

            root = tkinter.Tk()
            root.withdraw()
            w, h = root.winfo_screenwidth(), root.winfo_screenheight()
            root.destroy()
            return (0, 0, w, h)
        except Exception:
            return (0, 0, 1920, 1080)


class NullInput:
    """Stand-in used by tests and headless runs: nothing is ever pressed."""

    def __init__(self, on_button=None):
        self.on_button = on_button
        self.mods = set()
        self.locks = set()
        self.position = (0, 0)

    def start(self):
        pass

    def stop(self):
        pass

    def modifier_pressed(self, name):
        return name in self.mods

    def lock_on(self, name):
        return name in self.locks

    def cursor_position(self):
        return self.position

    def screen_rect(self, virtual=False):
        return (0, 0, 1920, 1080)
