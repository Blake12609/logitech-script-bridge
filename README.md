<p align="center">
  <img src="docs/icon.png" width="96" alt="">
</p>

<h1 align="center">Logitech Script Bridge</h1>

<p align="center">
  Run <b>Logitech G HUB / LGS Lua scripts</b> with <b>any mouse</b>, sending the output through a MAKCU, KMBox, ESP32-S3, Arduino or Raspberry Pi Pico.
</p>

<p align="center">
  <a href="https://github.com/Blake12609/logitech-script-bridge/releases/latest"><img src="https://img.shields.io/github/v/release/Blake12609/logitech-script-bridge?label=download&color=4f8cff" alt="latest release"></a>
  <a href="https://github.com/Blake12609/logitech-script-bridge/actions/workflows/build.yml"><img src="https://github.com/Blake12609/logitech-script-bridge/actions/workflows/build.yml/badge.svg" alt="build"></a>
  <img src="https://img.shields.io/badge/Windows-64--bit%20%7C%2032--bit-0078d4" alt="Windows 64-bit and 32-bit">
  <img src="https://img.shields.io/badge/portable-no%20install-3fb950" alt="portable">
</p>

<p align="center">
  <img src="docs/screenshot.png" width="760" alt="Logitech Script Bridge running a script">
</p>

## What it does

Logitech mice can run Lua scripts (rapid fire, macros, button remaps...) through
G HUB. This app runs those same scripts for everyone else:

1. It runs your `.lua` script with the same **G-series Lua API** as G HUB.
2. It watches your real mouse buttons and calls the script's `OnEvent` like G HUB does.
3. It sends the script's clicks, moves and key presses out through a **USB device**,
   which the PC sees as a real mouse and keyboard.

It's a **single portable `.exe`** of about 700 KB, written in C++ with Lua built in.
There's no installer, no .NET and no Python, and it has no dependencies beyond Windows itself.

## Download

Get the latest version from the **[Releases page](https://github.com/Blake12609/logitech-script-bridge/releases/latest)**:

| File | For |
|------|-----|
| `LogitechScriptBridge-x64.exe` | 64-bit Windows (almost every PC) |
| `LogitechScriptBridge-x86.exe` | 32-bit Windows |
| `LogitechScriptBridge-vX.Y.Z.zip` | Both exes, example scripts, firmware and an empty `scripts\` + `configs\` folder |

Windows SmartScreen may warn because the exe isn't code-signed: click *More info* and then *Run anyway*.

## Supported hardware

| Output device | Mouse | Keyboard | Setup |
|---|:---:|:---:|---|
| **MAKCU** | ✅ | via software* | Plug in, pick its COM port |
| **KMBox B / B+ / B Pro** | ✅ | via software* | Plug in, pick its COM port |
| **ESP32-S3** | ✅ | ✅ | Flash [`firmware/esp32s3_bridge`](firmware/esp32s3_bridge) |
| **Raspberry Pi Pico / RP2040** | ✅ | ✅ | Flash [`firmware/arduino_hid_bridge`](firmware/arduino_hid_bridge) |
| **Arduino Leonardo / Micro / Pro Micro** | ✅ (no back/forward) | ✅ | Flash [`firmware/arduino_hid_bridge`](firmware/arduino_hid_bridge) |
| **Software** (Windows `SendInput`) | ✅ | ✅ | Nothing. Many games ignore injected input, so this is mainly for testing |
| **Dry run** | log only | log only | Nothing. Shows every action in the log instead of sending it |

\* These devices are mouse-only. By default the app types the script's key presses
with Windows `SendInput` instead. You can turn that off.

Any other device that speaks the MAKCU/KMBox `km.move(x,y)` / `km.left(1)` serial
protocol should work with the MAKCU or KMBox setting.

<p align="center"><img src="docs/devices.png" width="560" alt="device picker"></p>

## Quick start

1. Download and run `LogitechScriptBridge-x64.exe`. Put it in its own folder: it saves its settings there.
2. **Open script**: pick your `.lua` file (the same one you'd paste into G HUB). You can also drag it onto
   the window, or keep your scripts in a `scripts\` folder next to the exe and pick them from the ⌄ menu.
3. **Output device**: pick your hardware and its COM port (the ⌄ menu lists connected ports).
   Press **Test**: the mouse pointer should wiggle right and back.
4. Press **Start** (F5). Script output (`OutputLogMessage`) appears in the log.

### Save and load configs

A config remembers the script, device, port and options. Use **Save config** (Ctrl+S), **Save as…**
(Ctrl+Shift+S) and **Load config** (Ctrl+L). You can also drop a `.ini` file on the window.
Configs go to a `configs\` folder next to the exe by default. Scripts stored next to them are
saved as relative paths, so you can move the whole folder or carry it on a USB stick.
The title bar shows `*` when the settings differ from the loaded config.

Turn on **Start the script when the app opens** and the app is ready to go as soon as you launch it.

### Randomize movement

**Randomize movement (± px)** adds a small random offset of up to X pixels sideways and Y pixels
up/down to every mouse movement the script makes. That gives drawing scripts in Paint and similar
apps a hand-drawn look. The pointer wobbles around the path the script asked for and never drifts
further than X / Y away from it, even over long strokes. Set both to 0 (the default) for exact
movement. Try [`examples/paint_draw_line.lua`](examples/paint_draw_line.lua) with X 3, Y 2.

### Mice with more than five buttons

G HUB reports buttons 6 and up for Logitech mice with extra buttons. Windows only
reports five, so the app has a workaround: in your mouse's own software, bind the
extra buttons to the keys **F13–F24**. Then set *F13–F24 keys* to:

- **Extra mouse buttons 6–17**: F13 becomes button 6, F14 becomes button 7, and so on (`MOUSE_BUTTON_PRESSED`, `IsMouseButtonPressed(6)`).
- **G-keys G1–G12**: F13 becomes G1 and so on (`G_PRESSED` / `G_RELEASED`, family `"kb"`).

While a script runs, those keys go to the script instead of other programs.

## How scripts behave (same as G HUB)

- `OnEvent(event, arg, family)` receives `PROFILE_ACTIVATED`, `PROFILE_DEACTIVATED`,
  `MOUSE_BUTTON_PRESSED` / `MOUSE_BUTTON_RELEASED` and `G_PRESSED` / `G_RELEASED`.
- Events are handled one at a time on a single script thread, so the usual
  `repeat ... Sleep(10) until not IsMouseButtonPressed(5)` loops work unchanged.
- Left click events are only reported after `EnablePrimaryMouseButtonEvents(true)`.
- Button numbers follow Logitech's convention:

  | Button  | `OnEvent` arg | `PressMouseButton` / `IsMouseButtonPressed` |
  |---------|:---:|:---:|
  | Left    | 1 | 1 |
  | Right   | 2 | 3 |
  | Middle  | 3 | 2 |
  | Back    | 4 | 4 |
  | Forward | 5 | 5 |

- Clicks sent by the script come back to the PC like real clicks. The app
  recognises them and does not feed them back into `OnEvent`.
- **Stop** (Shift+F5) interrupts a running handler, even a `while true do end` loop.
  It gives the script up to 1 second of `PROFILE_DEACTIVATED`, then releases every
  button and key the script still held. **Reload** (Ctrl+R) restarts the script after you edit it.
- `Sleep()` has 1 ms resolution.

### Supported API

`OutputLogMessage`, `OutputDebugMessage`, `ClearLog`, `Sleep`, `GetRunningTime`, `GetDate`,
`PressKey`, `ReleaseKey`, `PressAndReleaseKey`, `IsModifierPressed`, `IsKeyLockOn`,
`PressMouseButton`, `ReleaseMouseButton`, `PressAndReleaseMouseButton`, `IsMouseButtonPressed`,
`MoveMouseRelative`, `MoveMouseWheel`, `MoveMouseTo`, `MoveMouseToVirtual`, `GetMousePosition`,
`EnablePrimaryMouseButtonEvents`, `GetMKeyState`, `SetMKeyState`.

Keys can be given as names (`"lshift"`, `"a"`, `"f5"`, `"spacebar"`) or scancodes (`0x1E`).
Scripts run on Lua 5.4, with the Lua 5.1 names older scripts expect (`unpack`, `loadstring`,
`math.pow`) still available.

These do nothing because there is no Logitech hardware to drive:
`OutputLCDMessage`, `ClearLCD`, `SetBacklightColor`, `PlayMacro`/`PressMacro`/`AbortMacro`
(G HUB macros live in G HUB, not in the script), and the DPI functions.

As in G HUB, scripts run in a sandbox: `io`, `os.execute`, `require`, `debug` and
precompiled bytecode are not available.

## Flashing the firmware

Both firmwares make the board a USB mouse and keyboard that takes commands over its USB serial port.

**ESP32-S3**: open [`firmware/esp32s3_bridge/esp32s3_bridge.ino`](firmware/esp32s3_bridge/esp32s3_bridge.ino) in the Arduino IDE
with the ESP32 board package. Choose board **ESP32S3 Dev Module**, *USB Mode: USB-OTG (TinyUSB)* and
*USB CDC On Boot: Enabled*, then upload. Use the board's native **USB** port (not *COM/UART*).

**Arduino Leonardo / Micro / Pro Micro / Raspberry Pi Pico**: open
[`firmware/arduino_hid_bridge/arduino_hid_bridge.ino`](firmware/arduino_hid_bridge/arduino_hid_bridge.ino),
pick your board and upload. For the Pico, install Earle Philhower's *Raspberry Pi Pico/RP2040* core.
The stock AVR Mouse library only has three buttons, so back/forward clicks need an RP2040 board.

Serial protocol (one command per line, `\r\n`), the same commands as MAKCU plus keyboard:

```
km.move(x,y)   km.wheel(n)   km.left(1|0)   km.right(1|0)   km.middle(1|0)
km.side1(1|0)  km.side2(1|0) kb.down(hid)   kb.up(hid)      km.version()
```

## How it's tested

The tests run on every push, on Windows (64-bit and 32-bit) and Linux:

- **Engine**: G HUB behaviour against scripted input, including button numbering, hold loops,
  Stop inside endless loops, releasing held input, the sandbox, extra buttons and G-keys.
- **Wire format**: the exact bytes written to a serial port, checked through a virtual port.
- **Firmware, end to end**: both sketches are compiled against a stand-in Arduino library.
  A Lua script runs in the real engine, its commands are fed into the firmware, and the test
  checks the USB mouse and keyboard actions that come out.
- **Configs**: save/load round trips, relative paths and hand-edited files.

The GUI has been exercised under Wine. **None of the devices has been tested on
physical hardware yet.** If you try one, please open an issue saying whether it worked.

## Limitations

- Windows 10 and 11. Windows 7 and 8.1 should work but haven't been tested.
- `MoveMouseTo` is approximate: hardware mice only move relatively, so the app
  steers toward the target (pointer acceleration can make it land a few pixels off).
- Extra buttons beyond five need the F13–F24 workaround described above.

Game and anti-cheat rules apply to your scripts as they would with a Logitech mouse.
Check the rules of whatever you use this with.

## Building

Needs CMake 3.16+ and a C++17 compiler. Lua 5.4 is vendored in `third_party/lua`.

```bat
:: Visual Studio (Developer Command Prompt); use -A Win32 for the 32-bit build
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

```bash
# MinGW-w64 cross-compile from Linux (64-bit / 32-bit)
cmake -S . -B build-win   -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake      && cmake --build build-win
cmake -S . -B build-win32 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-i686.cmake && cmake --build build-win32

# tests on Linux/macOS
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

To publish a release, open **Actions → build → Run workflow** and enter a version (e.g. `v1.2.0`), or push a `v*` tag.

```
src/
  engine.cpp       Lua runtime + G-series API, event queue, stop handling
  backend.cpp      devices, MAKCU/KMBox km.* protocol, dry run
  serial.cpp       serial ports (Win32 + POSIX)
  config.cpp       config files, portable relative paths
  keys.cpp         key names <-> scancodes <-> HID usage IDs
  platform_win.cpp mouse/keyboard hooks, keyboard state, SendInput output
  gui_win.cpp      the window
firmware/
  esp32s3_bridge/      ESP32-S3 sketch
  arduino_hid_bridge/  Leonardo / Pro Micro / Pi Pico sketch
examples/              sample scripts
tests/                 engine, config, serial and firmware tests
```

## License

MIT, see [LICENSE](LICENSE). Lua is © Lua.org, PUC-Rio, under the MIT license ([third_party/lua/LICENSE](third_party/lua/LICENSE)).
