# Logitech Script Bridge

Run **Logitech G Hub / LGS Lua scripts with any mouse**. The app runs your
script on the PC, watches your real mouse buttons, and sends the script's
clicks, moves and key presses out through a small USB device:

- **MAKCU** (stock firmware, mouse only)
- **ESP32-S3** with the included [bridge firmware](firmware/esp32s3_bridge) (mouse + keyboard)
- **Software**: no hardware, uses normal OS input injection (for testing)
- **Dry run**: only logs what would have been sent

![screenshot](docs/screenshot.png)

## Quick start

```bash
pip install -r requirements.txt
python -m logitech_script_bridge          # opens the window
```

1. Pick your `.lua` script (the same file you would paste into G Hub).
2. Pick the device and its serial port (COMx on Windows, `/dev/ttyACM0` on Linux).
3. Press **Start**. Script output (`OutputLogMessage`) appears in the log.

Command-line use works too:

```bash
python -m logitech_script_bridge --list-ports
python -m logitech_script_bridge examples/side_button_copy_paste.lua --device makcu --port COM5
python -m logitech_script_bridge examples/log_everything.lua --device dry-run
```

## How it behaves (same as G Hub)

- `OnEvent(event, arg, family)` receives `PROFILE_ACTIVATED`, `PROFILE_DEACTIVATED`,
  `MOUSE_BUTTON_PRESSED` and `MOUSE_BUTTON_RELEASED`.
- Events are handled one at a time on a single script thread, so the usual
  `repeat ... Sleep(10) until not IsMouseButtonPressed(5)` loops work unchanged.
- Left click events are only reported after `EnablePrimaryMouseButtonEvents(true)`.
- Button numbers follow Logitech's (odd) convention:

  | Button  | `OnEvent` arg | `PressMouseButton` / `IsMouseButtonPressed` |
  |---------|:---:|:---:|
  | Left    | 1 | 1 |
  | Right   | 2 | 3 |
  | Middle  | 3 | 2 |
  | Back    | 4 | 4 |
  | Forward | 5 | 5 |

- Clicks sent by the script come back to the PC like real clicks. The app
  recognises them and does not feed them back into `OnEvent`.
- **Stop** interrupts a running handler (even a `while true do end` loop),
  gives the script up to 1 second of `PROFILE_DEACTIVATED`, then releases
  every button and key the script still held.

### Supported API

`OutputLogMessage`, `OutputDebugMessage`, `ClearLog`, `Sleep`, `GetRunningTime`, `GetDate`,
`PressKey`, `ReleaseKey`, `PressAndReleaseKey`, `IsModifierPressed`, `IsKeyLockOn`,
`PressMouseButton`, `ReleaseMouseButton`, `PressAndReleaseMouseButton`, `IsMouseButtonPressed`,
`MoveMouseRelative`, `MoveMouseWheel`, `MoveMouseTo`, `MoveMouseToVirtual`, `GetMousePosition`,
`EnablePrimaryMouseButtonEvents`, `GetMKeyState`, `SetMKeyState`.

Keys can be given as names (`"lshift"`, `"a"`, `"f5"`, `"spacebar"`) or scancodes (`0x1E`).

These do nothing here because there is no Logitech hardware to drive:
`OutputLCDMessage`, `ClearLCD`, `SetBacklightColor`, `PlayMacro`/`PressMacro`/`AbortMacro`
(G Hub macros live in G Hub, not in the script), and the DPI functions.

As in G Hub, scripts run in a sandbox: `io`, `os.execute`, `require` and similar are not available.

## Hardware

### MAKCU

Plug it in as usual, choose **MAKCU** and its COM port. The app sends the
standard `km.move`, `km.left`, `km.wheel`, ... commands at 115200 baud
(`--makcu-4m` switches the link to 4 Mbaud). Stock MAKCU firmware can't type,
so by default key presses from the script are sent in software instead.
Untick the checkbox (or pass `--no-key-fallback`) to drop them.

### ESP32-S3

Flash [`firmware/esp32s3_bridge/esp32s3_bridge.ino`](firmware/esp32s3_bridge/esp32s3_bridge.ino)
with the Arduino IDE (ESP32 board package, board **ESP32S3 Dev Module**,
*USB Mode: USB-OTG (TinyUSB)*, *USB CDC On Boot: Enabled*). Connect the
board's native USB port to the PC. It appears as a serial port plus a
USB mouse and keyboard. Choose **ESP32-S3 (bridge firmware)** and that port.

Serial protocol (one command per line):

```
km.move(x,y)   km.wheel(n)   km.left(1|0)   km.right(1|0)   km.middle(1|0)
km.side1(1|0)  km.side2(1|0) kb.down(hid)   kb.up(hid)      km.version()
```

## Limitations

- Only the five standard mouse buttons can trigger events. Extra
  G-buttons and G-keys of Logitech devices have no equivalent here.
- Input capture uses `pynput`: Windows works best. Linux needs X11. On macOS
  the app needs Accessibility permission.
- `MoveMouseTo` is approximate: hardware mice only move relatively, so the
  app steers toward the target (pointer acceleration can make it land a few pixels off).

Game and anti-cheat rules apply to your scripts as they would with a Logitech mouse.
Check the rules of whatever you use this with.

## Development

```bash
pip install -r requirements.txt pytest
python -m pytest
```

Layout:

```
logitech_script_bridge/
  engine.py    Lua runtime + G-series API, event queue, stop handling
  backends.py  MAKCU / ESP32 serial, software and dry-run outputs
  inputs.py    physical mouse/keyboard state (pynput, Win32)
  keys.py      key names <-> scancodes <-> HID usage IDs
  gui.py       Tkinter window
  __main__.py  command line
firmware/esp32s3_bridge/   Arduino sketch for the ESP32-S3
examples/                  sample scripts
tests/                     engine tests (no hardware needed)
```
