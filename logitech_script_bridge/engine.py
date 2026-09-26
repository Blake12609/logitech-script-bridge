"""Runs a Logitech G Hub / LGS Lua script and implements the G-series Lua API.

Like G Hub, all script code runs on one worker thread: events are queued and
OnEvent handles them one at a time, so a handler that loops with Sleep() and
IsMouseButtonPressed() behaves exactly as it does on a Logitech mouse.
"""

import collections
import queue
import sys
import threading
import time

from .inputs import EVENT_BUTTON
from .keys import key_to_hid

try:
    import lupa.lua51 as _lua  # G Hub uses Lua 5.1
except ImportError:  # older lupa builds only ship one Lua version
    import lupa as _lua

# PressMouseButton / IsMouseButtonPressed numbering (differs from OnEvent's!)
OUTPUT_BUTTON = {1: "left", 2: "middle", 3: "right", 4: "side1", 5: "side2"}
EVENT_BUTTON_NAME = {v: k for k, v in EVENT_BUTTON.items()}

_STOP = object()
_ECHO_WINDOW = 0.5  # seconds to wait for our own injected clicks to come back

PRELUDE = r"""
local api = __lsb
__lsb = nil
python = nil

-- keep only what G Hub scripts are allowed to use
io, dofile, loadfile, require, module, package = nil, nil, nil, nil, nil, nil
os = { time = os.time, clock = os.clock, date = os.date, difftime = os.difftime }
if not unpack then unpack = table.unpack end

local fmt = string.format
function OutputLogMessage(...) api.log(fmt(...)) end
function OutputDebugMessage(...) api.log(fmt(...)) end
function ClearLog() api.clear_log() end
function OutputLCDMessage(text, timeout) end
function ClearLCD() end
function Sleep(ms) api.sleep(ms or 0) end
function GetRunningTime() return api.running_time() end
function GetDate(f, t) return os.date(f, t) end

function PressKey(...) api.key(true, ...) end
function ReleaseKey(...) api.key(false, ...) end
function PressAndReleaseKey(...) api.key(true, ...); api.key(false, ...) end
function IsModifierPressed(name) return api.modifier(name) end
function IsKeyLockOn(name) return api.lock(name) end

function PressMouseButton(b) api.mbutton(b, true) end
function ReleaseMouseButton(b) api.mbutton(b, false) end
function PressAndReleaseMouseButton(b) api.mbutton(b, true); api.mbutton(b, false) end
function IsMouseButtonPressed(b) return api.mpressed(b) end
function MoveMouseRelative(x, y) api.move_rel(x, y) end
function MoveMouseWheel(n) api.wheel(n) end
function MoveMouseTo(x, y) api.move_to(x, y, false) end
function MoveMouseToVirtual(x, y) api.move_to(x, y, true) end
function GetMousePosition() return api.mouse_pos() end
function EnablePrimaryMouseButtonEvents(on) api.primary(on and true or false) end

local mstate = { kb = 1, lhc = 1, mouse = 1 }
function GetMKeyState(family) return mstate[family or "kb"] or 1 end
function SetMKeyState(m, family) mstate[family or "kb"] = m end

-- lighting / macro / DPI functions have nothing to drive here
function PlayMacro(name) api.log("PlayMacro(" .. tostring(name) .. ") is not supported\n") end
function PressMacro(name) PlayMacro(name) end
function ReleaseMacro(name) end
function AbortMacro() end
function SetBacklightColor() end
function SetMouseDPITable() end
function SetMouseDPITableIndex() end
function EnableHidEvents() end
function SetSteeringWheelProperty() end
"""


class ScriptStopped(Exception):
    pass


class ScriptEngine:
    def __init__(self, backend, log=None, input_monitor=None, keyboard_fallback=None):
        """
        backend            where mouse (and keyboard, if supported) output goes
        log                callable(str) receiving OutputLogMessage text and engine messages
        input_monitor      inputs.InputMonitor (or NullInput); None means create one
        keyboard_fallback  backend used for keys when `backend` can't type (e.g. MAKCU)
        """
        self.backend = backend
        self.log = log or (lambda s: (sys.stdout.write(s), sys.stdout.flush()))
        self.clear_log = lambda: None
        if input_monitor is None:
            from .inputs import InputMonitor

            input_monitor = InputMonitor(None)
        self.input = input_monitor
        self.input.on_button = self.on_physical_button
        self.keyboard_fallback = keyboard_fallback

        self._queue = queue.Queue()
        self._thread = None
        self._abort = threading.Event()
        self._deadline = None
        self._lock = threading.Lock()
        self._physical = set()     # button names held on the real mouse
        self._synthetic = set()    # button names held by the script
        self._keys_down = set()    # HID codes held by the script
        self._echo = collections.defaultdict(collections.deque)
        self._primary_events = False
        self._start_time = time.monotonic()
        self._warned_keyboard = False
        self.running = False

    # ------------------------------------------------------------ lifecycle
    def start(self, source, chunk_name="script"):
        if self.running:
            raise RuntimeError("already running")
        self._queue = queue.Queue()
        self._abort.clear()
        self._deadline = None
        self._primary_events = False
        self._start_time = time.monotonic()
        ready = threading.Event()
        self._start_error = None
        self._thread = threading.Thread(
            target=self._run, args=(source, chunk_name, ready), name="lua-script", daemon=True
        )
        self.running = True
        self._thread.start()
        ready.wait()
        if self._start_error:
            self.running = False
            raise self._start_error
        try:
            self.input.start()
        except Exception:
            self.stop()
            raise
        self.post("PROFILE_ACTIVATED", 0, "")

    def stop(self, timeout=3.0):
        if not self.running:
            return
        self.input.stop()
        self._abort.set()   # breaks out of the handler that is running right now
        self._queue.put(_STOP)
        self._thread.join(timeout)
        self.running = False

    def wait(self):
        """Block until the script thread exits (used by the CLI)."""
        while self._thread and self._thread.is_alive():
            self._thread.join(0.2)

    def post(self, event, arg=0, family=""):
        if self.running:
            self._queue.put((event, arg, family))

    # ------------------------------------------------------------ physical input
    def on_physical_button(self, name, pressed):
        with self._lock:
            echoes = self._echo[name]
            now = time.monotonic()
            while echoes and echoes[0][1] < now:
                echoes.popleft()
            if echoes and echoes[0][0] == pressed:
                echoes.popleft()  # this is our own injected click coming back
                return
            if pressed:
                self._physical.add(name)
            else:
                self._physical.discard(name)
        num = EVENT_BUTTON[name]
        if num == 1 and not self._primary_events:
            return
        self.post("MOUSE_BUTTON_PRESSED" if pressed else "MOUSE_BUTTON_RELEASED", num, "mouse")

    # ------------------------------------------------------------ worker thread
    def _run(self, source, chunk_name, ready):
        if sys.platform == "win32":
            import ctypes

            ctypes.windll.winmm.timeBeginPeriod(1)  # 1 ms Sleep() resolution instead of ~15 ms
        try:
            lua = _lua.LuaRuntime(
                register_eval=False,
                register_builtins=False,
                unpack_returned_tuples=True,
                attribute_filter=_deny_attributes,
            )
            lua.globals()["__lsb"] = lua.table_from(self._api())
            lua.execute(PRELUDE)
            # abort long loops even if they never call Sleep()
            lua.execute("local check = ...; debug.sethook(function() check() end, '', 10000); debug = nil", self._check_abort)
            loader = lua.globals().loadstring
            fn = loader(source, "=" + chunk_name)
            if isinstance(fn, tuple):  # (nil, message) on a syntax error
                raise SyntaxError(fn[1])
            fn()
            on_event = lua.globals().OnEvent
            if on_event is None:
                raise RuntimeError("script does not define OnEvent(event, arg, family)")
        except Exception as e:  # compile/load error: report back to start()
            self._start_error = e
            ready.set()
            return
        ready.set()
        self.log("Script loaded: %s\n" % chunk_name)

        try:
            while True:
                item = self._queue.get()
                if item is _STOP:
                    # like G Hub: give the script a short moment to clean up
                    self._abort.clear()
                    self._deadline = time.monotonic() + 1.0
                    self._call(on_event, ("PROFILE_DEACTIVATED", 0, ""))
                    break
                if self._abort.is_set():
                    continue
                self._call(on_event, item)
        finally:
            self._release_all()
            self.running = False
            self.log("Script stopped.\n")

    def _call(self, on_event, item):
        event, arg, family = item
        try:
            on_event(event, arg, family)
        except Exception as e:
            if isinstance(e, ScriptStopped) or "ScriptStopped" in str(e):
                return
            self.log("Script error in OnEvent(%s, %s): %s\n" % (event, arg, e))

    def _check_abort(self, *args):
        if self._abort.is_set():
            raise ScriptStopped()
        if self._deadline is not None and time.monotonic() > self._deadline:
            raise ScriptStopped()

    # ------------------------------------------------------------ API impl
    def _api(self):
        return {
            "log": lambda s: self.log(str(s)),
            "clear_log": lambda: self.clear_log(),
            "sleep": self._sleep,
            "running_time": lambda: int((time.monotonic() - self._start_time) * 1000),
            "key": self._key,
            "modifier": self._modifier,
            "lock": lambda name: self.input.lock_on(str(name).lower()),
            "mbutton": self._mbutton,
            "mpressed": self._mpressed,
            "move_rel": lambda x, y: self.backend.move(int(x), int(y)),
            "wheel": lambda n: self.backend.wheel(int(n)),
            "move_to": self._move_to,
            "mouse_pos": self._mouse_pos,
            "primary": self._set_primary,
        }

    def _set_primary(self, on):
        self._primary_events = bool(on)

    def _sleep(self, ms):
        self._check_abort()
        ms = float(ms)
        if ms > 0:
            end = time.monotonic() + ms / 1000.0
            if self._deadline is not None:
                end = min(end, self._deadline)
            self._abort.wait(max(0.0, end - time.monotonic()))
        self._check_abort()

    def _key(self, down, *keys):
        for k in keys:
            try:
                hid = key_to_hid(k)
            except ValueError as e:
                self.log("%s\n" % e)
                continue
            out = self.backend if self.backend.supports_keyboard else self.keyboard_fallback
            if out is None:
                if not self._warned_keyboard:
                    self.log("This device cannot send keys; keyboard commands are ignored.\n")
                    self._warned_keyboard = True
                continue
            out.key(hid, down)
            (self._keys_down.add if down else self._keys_down.discard)(hid)

    def _modifier(self, name):
        name = str(name).lower()
        if name in ("shift", "ctrl", "alt"):
            return self.input.modifier_pressed("l" + name) or self.input.modifier_pressed("r" + name)
        return self.input.modifier_pressed(name)

    def _mbutton(self, num, down):
        name = OUTPUT_BUTTON.get(int(num))
        if name is None:
            return
        with self._lock:
            self._echo[name].append((down, time.monotonic() + _ECHO_WINDOW))
            (self._synthetic.add if down else self._synthetic.discard)(name)
        self.backend.button(name, down)

    def _mpressed(self, num):
        name = OUTPUT_BUTTON.get(int(num))
        with self._lock:
            return name in self._physical or name in self._synthetic

    def _mouse_pos(self):
        left, top, w, h = self.input.screen_rect(False)
        x, y = self.input.cursor_position()
        return (_clamp(round((x - left) * 65535 / max(w - 1, 1))),
                _clamp(round((y - top) * 65535 / max(h - 1, 1))))

    def _move_to(self, x, y, virtual):
        # Hardware mice only move relatively, so steer towards the target.
        left, top, w, h = self.input.screen_rect(virtual)
        tx = left + round(float(x) * (w - 1) / 65535)
        ty = top + round(float(y) * (h - 1) / 65535)
        for _ in range(3):
            cx, cy = self.input.cursor_position()
            dx, dy = tx - cx, ty - cy
            if not dx and not dy:
                break
            self.backend.move(dx, dy)
            time.sleep(0.01)

    def _release_all(self):
        for name in list(self._synthetic):
            try:
                self.backend.button(name, False)
            except Exception:
                pass
        self._synthetic.clear()
        for hid in list(self._keys_down):
            out = self.backend if self.backend.supports_keyboard else self.keyboard_fallback
            try:
                out.key(hid, False)
            except Exception:
                pass
        self._keys_down.clear()


def _clamp(v):
    return max(0, min(65535, int(v)))


def _deny_attributes(obj, attr, is_setting):
    raise AttributeError("access to Python attributes is not allowed")

