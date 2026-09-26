# Logitech Script Bridge

Run **Logitech G Hub / LGS Lua scripts with any mouse**. The app runs your
script, watches your real mouse buttons, and sends the script's clicks, moves
and key presses out through a small USB device:

- **MAKCU** (stock firmware, mouse only)
- **ESP32-S3** with the included [bridge firmware](firmware/esp32s3_bridge) (mouse + keyboard)
- **Software**: no hardware, uses Windows `SendInput` (for testing)
- **Dry run**: only logs what would have been sent

It's a single portable `LogitechScriptBridge.exe` (about 600 KB, written in C++ with
Lua built in). There's no installer, no .NET and no Python, and it has no dependencies
beyond Windows itself. Put it in any folder or on a USB stick and run it. Settings are
saved to `LogitechScriptBridge.ini` next to the exe.

![screenshot](docs/screenshot.png)

## Quick start

1. Download `LogitechScriptBridge.exe` from the
   [Releases](../../releases) page, or from the latest run under the
   [Actions](../../actions) tab (artifact *LogitechScriptBridge*).
2. Run it and pick your `.lua` script (the same file you would paste into
   G Hub). You can also drop the file on the window or pass it on the command line.
3. Pick the device and its COM port, then press **Start**. Script output
   (`OutputLogMessage`) appears in the log.

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
- `Sleep()` has 1 ms resolution.

### Supported API

`OutputLogMessage`, `OutputDebugMessage`, `ClearLog`, `Sleep`, `GetRunningTime`, `GetDate`,
`PressKey`, `ReleaseKey`, `PressAndReleaseKey`, `IsModifierPressed`, `IsKeyLockOn`,
`PressMouseButton`, `ReleaseMouseButton`, `PressAndReleaseMouseButton`, `IsMouseButtonPressed`,
`MoveMouseRelative`, `MoveMouseWheel`, `MoveMouseTo`, `MoveMouseToVirtual`, `GetMousePosition`,
`EnablePrimaryMouseButtonEvents`, `GetMKeyState`, `SetMKeyState`.

Keys can be given as names (`"lshift"`, `"a"`, `"f5"`, `"spacebar"`) or scancodes (`0x1E`).

Scripts run on Lua 5.4, with the Lua 5.1 names older scripts expect
(`unpack`, `loadstring`, `math.pow`) still available.

These do nothing because there is no Logitech hardware to drive:
`OutputLCDMessage`, `ClearLCD`, `SetBacklightColor`, `PlayMacro`/`PressMacro`/`AbortMacro`
(G Hub macros live in G Hub, not in the script), and the DPI functions.

As in G Hub, scripts run in a sandbox: `io`, `os.execute`, `require`, `debug` and
precompiled bytecode are not available.

## Hardware

### MAKCU

Plug it in as usual, choose **MAKCU** and its COM port. The app sends the
standard `km.move`, `km.left`, `km.wheel`, ... commands at 115200 baud. Stock
MAKCU firmware can't type, so by default key presses from the script are sent
in software instead. Untick the checkbox to drop them.

### ESP32-S3

Flash [`firmware/esp32s3_bridge/esp32s3_bridge.ino`](firmware/esp32s3_bridge/esp32s3_bridge.ino)
with the Arduino IDE (ESP32 board package, board **ESP32S3 Dev Module**,
*USB Mode: USB-OTG (TinyUSB)*, *USB CDC On Boot: Enabled*). Connect the
board's native USB port to the PC. It appears as a COM port plus a USB mouse
and keyboard. Choose **ESP32-S3 (bridge firmware)** and that port.

Serial protocol (one command per line, `\r\n`):

```
km.move(x,y)   km.wheel(n)   km.left(1|0)   km.right(1|0)   km.middle(1|0)
km.side1(1|0)  km.side2(1|0) kb.down(hid)   kb.up(hid)      km.version()
```

## Limitations

- Only the five standard mouse buttons can trigger events. Extra
  G-buttons and G-keys of Logitech devices have no equivalent here.
- `MoveMouseTo` is approximate: hardware mice only move relatively, so the
  app steers toward the target (pointer acceleration can make it land a few pixels off).
- The app is Windows-only. The script engine itself is portable C++ and its tests
  also run on Linux.

Game and anti-cheat rules apply to your scripts as they would with a Logitech mouse.
Check the rules of whatever you use this with.

## Building

Needs CMake 3.16+ and a C++17 compiler. Lua 5.4 is vendored in `third_party/lua`.

```bat
:: Visual Studio (Developer Command Prompt)
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

```bash
# MinGW-w64 cross-compile from Linux
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake
cmake --build build-win

# engine tests on Linux/macOS
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

GitHub Actions builds the exe on every push. Pushing a tag like `v1.0.0` also
publishes it as a release.

```
src/
  engine.cpp       Lua runtime + G-series API, event queue, stop handling
  backend.cpp      MAKCU / ESP32 serial protocol, dry run
  serial.cpp       serial ports (Win32 + POSIX)
  keys.cpp         key names <-> scancodes <-> HID usage IDs
  platform_win.cpp mouse hook, keyboard state, SendInput output
  gui_win.cpp      the window
firmware/esp32s3_bridge/   Arduino sketch for the ESP32-S3
examples/                  sample scripts
tests/engine_tests.cpp     engine tests (no hardware needed)
```
