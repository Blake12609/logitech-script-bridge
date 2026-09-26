import threading
import time

import pytest

from logitech_script_bridge.backends import DryRunBackend
from logitech_script_bridge.engine import ScriptEngine
from logitech_script_bridge.inputs import NullInput


class Harness:
    def __init__(self, source, keyboard=True):
        self.backend = DryRunBackend()
        self.backend.supports_keyboard = keyboard
        self.logs = []
        self.input = NullInput()
        self.engine = ScriptEngine(self.backend, log=self.logs.append, input_monitor=self.input)
        self.engine.start(source, "test.lua")

    def settle(self, seconds=0.1):
        time.sleep(seconds)

    def text(self):
        return "".join(self.logs)

    def stop(self):
        self.engine.stop()


@pytest.fixture
def run():
    harnesses = []

    def make(source, **kw):
        h = Harness(source, **kw)
        harnesses.append(h)
        return h

    yield make
    for h in harnesses:
        h.stop()


def test_profile_activated_and_log(run):
    h = run('function OnEvent(e, a, f) OutputLogMessage("%s %d\\n", e, a) end')
    h.settle()
    assert "PROFILE_ACTIVATED 0\n" in h.text()
    h.stop()
    assert "PROFILE_DEACTIVATED 0\n" in h.text()


def test_side_button_presses_key_combo(run):
    h = run("""
    function OnEvent(event, arg)
        if event == "MOUSE_BUTTON_PRESSED" and arg == 4 then
            PressAndReleaseKey("lctrl", "c")
        end
    end""")
    h.engine.on_physical_button("side1", True)
    h.settle()
    assert h.backend.sent == [
        ("key", "0xE0", 1), ("key", "0x06", 1), ("key", "0xE0", 0), ("key", "0x06", 0),
    ]


def test_scancodes_are_accepted(run):
    h = run('function OnEvent(e) if e == "PROFILE_ACTIVATED" then PressKey(0x1E) ReleaseKey(0x1E) end end')
    h.settle()
    assert h.backend.sent == [("key", "0x04", 1), ("key", "0x04", 0)]


def test_primary_button_needs_enable(run):
    h = run("""
    function OnEvent(event, arg)
        if event == "MOUSE_BUTTON_PRESSED" then OutputLogMessage("press %d\\n", arg) end
    end""")
    h.engine.on_physical_button("left", True)
    h.engine.on_physical_button("right", True)
    h.settle()
    assert "press 1" not in h.text()
    assert "press 2" in h.text()


def test_button_numbering_matches_logitech(run):
    # OnEvent: 2 = right, 3 = middle.  Press/IsMouseButtonPressed: 2 = middle, 3 = right.
    h = run("""
    function OnEvent(event, arg)
        if event == "MOUSE_BUTTON_PRESSED" and arg == 2 then
            OutputLogMessage("right=%s middle=%s\\n", tostring(IsMouseButtonPressed(3)), tostring(IsMouseButtonPressed(2)))
            PressMouseButton(2)
            ReleaseMouseButton(2)
        end
    end""")
    h.engine.on_physical_button("right", True)
    h.settle()
    assert "right=true middle=false" in h.text()
    assert h.backend.sent == [("button", "middle", 1), ("button", "middle", 0)]


def test_hold_loop_runs_until_release(run):
    h = run("""
    function OnEvent(event, arg)
        if event == "MOUSE_BUTTON_PRESSED" and arg == 5 then
            repeat
                MoveMouseRelative(0, 1)
                Sleep(10)
            until not IsMouseButtonPressed(5)
            OutputLogMessage("done\\n")
        end
    end""")
    h.engine.on_physical_button("side2", True)
    h.settle(0.15)
    h.engine.on_physical_button("side2", False)
    h.settle(0.05)
    assert "done" in h.text()
    moves = [c for c in h.backend.sent if c[0] == "move"]
    assert 5 <= len(moves) <= 30


def test_injected_clicks_are_not_reported_back(run):
    h = run("""
    function OnEvent(event, arg)
        if event == "PROFILE_ACTIVATED" then EnablePrimaryMouseButtonEvents(true) end
        if event == "MOUSE_BUTTON_PRESSED" then OutputLogMessage("press %d\\n", arg) end
        if event == "MOUSE_BUTTON_PRESSED" and arg == 4 then PressAndReleaseMouseButton(1) end
    end""")
    h.settle()
    h.engine.on_physical_button("side1", True)
    h.settle()
    # the hardware click shows up on the PC like a real one; it must be swallowed
    h.engine.on_physical_button("left", True)
    h.engine.on_physical_button("left", False)
    h.settle()
    assert "press 4" in h.text()
    assert "press 1" not in h.text()


def test_stop_interrupts_busy_loop_and_releases_buttons(run):
    h = run("""
    function OnEvent(event, arg)
        if event == "PROFILE_ACTIVATED" then
            PressMouseButton(1)
            while true do end
        end
    end""")
    h.settle()
    t = threading.Thread(target=h.stop)
    t.start()
    t.join(3)
    assert not t.is_alive()
    assert h.backend.sent[-1] == ("button", "left", 0)


def test_keyboard_fallback_when_device_cannot_type(run):
    h = run('function OnEvent(e) if e == "PROFILE_ACTIVATED" then PressAndReleaseKey("a") end end', keyboard=False)
    h.settle()
    assert h.backend.sent == []
    assert "cannot send keys" in h.text()


def test_modifiers(run):
    h = run('function OnEvent(e, a) if a == 4 then OutputLogMessage("%s\\n", tostring(IsModifierPressed("shift"))) end end')
    h.input.mods.add("rshift")
    h.engine.on_physical_button("side1", True)
    h.settle()
    assert "true" in h.text()


def test_sandbox(run):
    h = run("""
    OutputLogMessage("io=%s python=%s execute=%s debug=%s\\n",
        tostring(io), tostring(python), tostring(os.execute), tostring(debug))
    local ok = pcall(function() return OutputLogMessage.__globals__ end)
    function OnEvent() end""")
    h.settle()
    assert "io=nil python=nil execute=nil debug=nil" in h.text()


def test_syntax_error_reported():
    with pytest.raises(SyntaxError) as info:
        Harness("function OnEvent(")
    assert "test.lua" in str(info.value)


def test_mouse_position_and_move_to(run):
    h = run("""
    function OnEvent(e)
        if e == "PROFILE_ACTIVATED" then
            local x, y = GetMousePosition()
            OutputLogMessage("%d %d\\n", x, y)
        end
    end""")
    h.input.position = (0, 0)
    h.settle()
    assert "0 0" in h.text()
