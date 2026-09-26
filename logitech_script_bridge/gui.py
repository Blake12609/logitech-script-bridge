"""A deliberately small Tkinter window: pick a script, pick a device, press Start."""

import json
import os
import queue
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinter.scrolledtext import ScrolledText

from .backends import DEVICES, SoftwareBackend, create_backend, list_serial_ports
from .engine import ScriptEngine

SETTINGS = os.path.join(os.path.expanduser("~"), ".logitech_script_bridge.json")


class App:
    def __init__(self, root):
        self.root = root
        self.engine = None
        self.backend = None
        self.fallback = None
        self.logq = queue.Queue()

        root.title("Logitech Script Bridge")
        root.minsize(560, 380)
        frm = ttk.Frame(root, padding=10)
        frm.pack(fill="both", expand=True)
        frm.columnconfigure(1, weight=1)

        s = self._load_settings()
        self.script = tk.StringVar(value=s.get("script", ""))
        self.device = tk.StringVar(value=DEVICES.get(s.get("device"), DEVICES["makcu"]))
        self.port = tk.StringVar(value=s.get("port", ""))
        self.baud = tk.StringVar(value=str(s.get("baud", 115200)))
        self.key_fallback = tk.BooleanVar(value=s.get("key_fallback", True))

        ttk.Label(frm, text="Lua script").grid(row=0, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.script).grid(row=0, column=1, sticky="ew", padx=5)
        ttk.Button(frm, text="Browse…", command=self.browse).grid(row=0, column=2)

        ttk.Label(frm, text="Device").grid(row=1, column=0, sticky="w", pady=4)
        ttk.Combobox(frm, textvariable=self.device, values=list(DEVICES.values()),
                     state="readonly").grid(row=1, column=1, sticky="ew", padx=5)

        ttk.Label(frm, text="Port").grid(row=2, column=0, sticky="w")
        self.port_box = ttk.Combobox(frm, textvariable=self.port)
        self.port_box.grid(row=2, column=1, sticky="ew", padx=5)
        ttk.Button(frm, text="Refresh", command=self.refresh_ports).grid(row=2, column=2)

        ttk.Label(frm, text="Baud").grid(row=3, column=0, sticky="w", pady=4)
        ttk.Entry(frm, textvariable=self.baud, width=10).grid(row=3, column=1, sticky="w", padx=5)

        ttk.Checkbutton(frm, text="Type keys in software if the device can't (MAKCU)",
                        variable=self.key_fallback).grid(row=4, column=1, sticky="w", padx=5)

        btns = ttk.Frame(frm)
        btns.grid(row=5, column=0, columnspan=3, sticky="w", pady=6)
        self.start_btn = ttk.Button(btns, text="Start", command=self.start)
        self.start_btn.pack(side="left")
        self.stop_btn = ttk.Button(btns, text="Stop", command=self.stop, state="disabled")
        self.stop_btn.pack(side="left", padx=5)
        ttk.Button(btns, text="Clear log", command=self.clear_log).pack(side="left")
        self.status = ttk.Label(btns, text="Stopped")
        self.status.pack(side="left", padx=10)

        self.log_box = ScrolledText(frm, height=14, state="disabled", font=("Consolas", 9))
        self.log_box.grid(row=6, column=0, columnspan=3, sticky="nsew")
        frm.rowconfigure(6, weight=1)

        self.refresh_ports()
        root.protocol("WM_DELETE_WINDOW", self.on_close)
        root.after(50, self._pump_log)

    # ----------------------------------------------------------------- helpers
    def _device_key(self):
        return next(k for k, v in DEVICES.items() if v == self.device.get())

    def _load_settings(self):
        try:
            with open(SETTINGS) as f:
                return json.load(f)
        except (OSError, ValueError):
            return {}

    def _save_settings(self):
        data = {"script": self.script.get(), "device": self._device_key(), "port": self.port.get(),
                "baud": self.baud.get(), "key_fallback": self.key_fallback.get()}
        try:
            with open(SETTINGS, "w") as f:
                json.dump(data, f, indent=2)
        except OSError:
            pass

    def log(self, text):
        self.logq.put(text)  # called from the script thread

    def _pump_log(self):
        chunks = []
        while not self.logq.empty():
            chunks.append(self.logq.get_nowait())
        if chunks:
            self.log_box.configure(state="normal")
            self.log_box.insert("end", "".join(chunks))
            self.log_box.see("end")
            self.log_box.configure(state="disabled")
        if self.engine and not self.engine.running and self.stop_btn["state"] == "normal":
            self._set_running(False)
        self.root.after(50, self._pump_log)

    def clear_log(self):
        self.log_box.configure(state="normal")
        self.log_box.delete("1.0", "end")
        self.log_box.configure(state="disabled")

    def browse(self):
        path = filedialog.askopenfilename(filetypes=[("Lua scripts", "*.lua"), ("All files", "*.*")])
        if path:
            self.script.set(path)

    def refresh_ports(self):
        ports = list_serial_ports()
        self.port_box["values"] = [dev for dev, _ in ports]
        if not self.port.get() and ports:
            self.port.set(ports[0][0])

    def _set_running(self, running):
        self.start_btn["state"] = "disabled" if running else "normal"
        self.stop_btn["state"] = "normal" if running else "disabled"
        self.status["text"] = "Running" if running else "Stopped"
        if not running:
            self._close_devices()

    def _close_devices(self):
        for dev in (self.backend, self.fallback):
            if dev is not None:
                try:
                    dev.close()
                except Exception:
                    pass
        self.backend = self.fallback = None

    # ----------------------------------------------------------------- actions
    def start(self):
        self._save_settings()
        try:
            with open(self.script.get(), encoding="utf-8-sig") as f:
                source = f.read()
        except OSError as e:
            messagebox.showerror("Script", "Cannot open script:\n%s" % e)
            return
        device = self._device_key()
        try:
            self.backend = create_backend(device, self.port.get() or None, int(self.baud.get() or 115200),
                                          log=self.log)
            self.backend.open()
            if not self.backend.supports_keyboard and self.key_fallback.get():
                self.fallback = SoftwareBackend()
                self.fallback.open()
            self.engine = ScriptEngine(self.backend, log=self.log, keyboard_fallback=self.fallback)
            self.engine.clear_log = lambda: self.root.after(0, self.clear_log)
            self.engine.start(source, os.path.basename(self.script.get()))
        except Exception as e:
            self._close_devices()
            messagebox.showerror("Start failed", str(e))
            return
        self.log("Running on %s\n" % DEVICES[device])
        self._set_running(True)

    def stop(self):
        if self.engine:
            self.engine.stop()
        self._set_running(False)

    def on_close(self):
        self.stop()
        self.root.destroy()


def run_gui():
    root = tk.Tk()
    App(root)
    root.mainloop()
    return 0
