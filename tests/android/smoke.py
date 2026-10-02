#!/usr/bin/env python3
"""Smoke test for the Android app on an emulator (run by CI).

Installs the APK, opens the app, switches to the Demo device, starts and stops
the example script and opens the editor, by tapping through the real UI.
Fails if a step doesn't show what it should, or if the app crashed.
"""
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

PKG = "io.github.blake12609.scriptbridge"
APK = sys.argv[1] if len(sys.argv) > 1 else "LogitechScriptBridge-android.apk"


def adb(*args, check=True):
    r = subprocess.run(["adb", *args], capture_output=True, text=True)
    if check and r.returncode != 0:
        sys.exit(f"adb {' '.join(args)} failed: {r.stderr}")
    return r.stdout


def nodes():
    adb("shell", "uiautomator", "dump", "/sdcard/ui.xml")
    xml = adb("shell", "cat", "/sdcard/ui.xml")
    return list(ET.fromstring(xml[xml.index("<"):]).iter("node"))


def find(text=None, desc=None, timeout=15):
    end = time.time() + timeout
    while time.time() < end:
        for n in nodes():
            if (text is not None and n.get("text") == text) or (desc is not None and n.get("content-desc") == desc):
                return n
        time.sleep(1)
    return None


def tap(node):
    x1, y1, x2, y2 = map(int, re.findall(r"\d+", node.get("bounds")))
    adb("shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))
    time.sleep(1.5)


def swipe(up):
    # scroll the main screen (the log card is below the fold)
    y1, y2 = (1600, 500) if up else (500, 1600)
    adb("shell", "input", "swipe", "540", str(y1), "540", str(y2), "300")
    time.sleep(1.5)


def step(name, node):
    if node is None:
        print(f"FAIL {name}")
        print(adb("logcat", "-d", "-t", "300", check=False))
        sys.exit(1)
    print(f"ok   {name}")
    return node


def crashed():
    log = adb("logcat", "-d", check=False)
    return "FATAL EXCEPTION" in log and PKG in log


adb("install", "-r", APK)
adb("shell", "pm", "grant", PKG, "android.permission.POST_NOTIFICATIONS", check=False)
adb("logcat", "-c")
adb("shell", "am", "start", "-W", "-n", f"{PKG}/.MainActivity")

step("app opens", find(text="Script Bridge", timeout=30))
step("first run loads an example", find(text="hold_to_autoclick"))
tap(step("device picker", find(text="MAKCU")))
tap(step("demo device in the list", find(text="Demo (no hardware)")))
step("demo selected", find(text="Demo"))
tap(step("start button", find(desc="Start")))
step("script running", find(text="RUNNING"))
swipe(up=True)
swipe(up=True)
step("engine log reaches the app", find(text="Script loaded: hold_to_autoclick.lua"))
step("script output in the log", find(text="Autoclicker ready - hold mouse button 5"))
swipe(up=False)
swipe(up=False)
tap(step("stop button", find(desc="Stop")))
step("script stopped", find(text="STOPPED"))
tap(step("edit button", find(text="Edit")))
step("editor shows the syntax check", find(text="No syntax errors"))
adb("shell", "input", "keyevent", "KEYCODE_BACK")
step("back on the main screen", find(text="Output device".upper()))

if crashed():
    print(adb("logcat", "-d", check=False))
    sys.exit("FAIL the app crashed")
if not adb("shell", "pidof", PKG, check=False).strip():
    sys.exit("FAIL the app is not running any more")
print("PASSED: Android smoke test")
