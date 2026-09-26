"""Command line entry point.

    python -m logitech_script_bridge                      open the GUI
    python -m logitech_script_bridge script.lua --device makcu --port COM5
    python -m logitech_script_bridge --list-ports
"""

import argparse
import signal
import sys

from .backends import DEVICES, SoftwareBackend, create_backend, list_serial_ports
from .engine import ScriptEngine


def main(argv=None):
    p = argparse.ArgumentParser(prog="logitech_script_bridge", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("script", nargs="?", help="Logitech Lua script to run (omit to open the GUI)")
    p.add_argument("--device", choices=list(DEVICES), default="makcu")
    p.add_argument("--port", help="serial port of the device, e.g. COM5 or /dev/ttyACM0")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--makcu-4m", action="store_true", help="switch the MAKCU link to 4 Mbaud")
    p.add_argument("--no-key-fallback", action="store_true",
                   help="drop key presses instead of typing them in software when the device can't")
    p.add_argument("--list-ports", action="store_true", help="list serial ports and exit")
    args = p.parse_args(argv)

    if args.list_ports:
        for dev, desc in list_serial_ports():
            print("%-15s %s" % (dev, desc))
        return 0

    if not args.script:
        from .gui import run_gui

        return run_gui()

    if args.device in ("makcu", "esp32") and not args.port:
        p.error("--port is required for --device %s (see --list-ports)" % args.device)

    def log(s):
        sys.stdout.write(s)
        sys.stdout.flush()

    with open(args.script, encoding="utf-8-sig") as f:
        source = f.read()

    backend = create_backend(args.device, args.port, args.baud, log=log, makcu_high_speed=args.makcu_4m)
    backend.open()
    fallback = None
    if not backend.supports_keyboard and not args.no_key_fallback:
        fallback = SoftwareBackend()
        fallback.open()

    engine = ScriptEngine(backend, log=log, keyboard_fallback=fallback)
    try:
        engine.start(source, args.script)
    except Exception as e:
        print("Could not load script: %s" % e)
        backend.close()
        return 1

    print("Running on %s. Press Ctrl+C to stop." % DEVICES[args.device])
    signal.signal(signal.SIGINT, lambda *a: engine.stop())
    engine.wait()
    backend.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
