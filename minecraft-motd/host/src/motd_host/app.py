import json
import queue
import re
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox

import serial
from serial.tools import list_ports


class SerialLink:
    def __init__(self, port, events):
        self.port = port
        self.events = events
        self.commands = queue.Queue()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        try:
            with serial.Serial(self.port, 115200, timeout=0.05, write_timeout=1) as device:
                self.events.put(("connected", self))
                buffer = bytearray()
                while not self.stop.is_set():
                    while not self.commands.empty():
                        command = self.commands.get_nowait()
                        encoded = (json.dumps(command, ensure_ascii=False) + "\n").encode("utf-8")
                        if device.write(encoded) != len(encoded):
                            raise serial.SerialException("串口命令发送中断")
                    buffer.extend(device.read(4096))
                    if len(buffer) > 524288:
                        raise serial.SerialException("设备响应超过 512 KiB")
                    while b"\n" in buffer:
                        line, _, remainder = buffer.partition(b"\n")
                        buffer = bytearray(remainder)
                        if not line.strip():
                            continue
                        try:
                            message = json.loads(line)
                        except (json.JSONDecodeError, UnicodeDecodeError):
                            self.events.put(("log", (self, line.decode("utf-8", errors="replace"))))
                            continue
                        if not isinstance(message, dict):
                            raise serial.SerialException("设备响应必须为 JSON 对象")
                        self.events.put(("message", (self, message)))
        except (serial.SerialException, OSError) as error:
            self.events.put(("error", (self, str(error))))
        finally:
            self.events.put(("disconnected", self))


class App:
    def __init__(self, root):
        self.root = root
        self.root.title("StickS3 · Minecraft MOTD")
        self.root.geometry("660x650")
        self.root.minsize(580, 540)
        self.events = queue.Queue()
        self.link = None
        self.connected = False
        self.pending = None
        self.sequence = 0
        self.config_ready = False
        self.password_set = False
        self.saved_ssid = ""
        self.port = tk.StringVar()
        self.status = tk.StringVar(value="选择 StickS3 串口")
        self.result_summary = tk.StringVar(value="等待设备响应")
        self.values = {
            "ssid": tk.StringVar(), "password": tk.StringVar(), "host": tk.StringVar(),
            "connect_host": tk.StringVar(), "port": tk.StringVar(value="25565"),
            "protocol": tk.StringVar(value="47"), "refresh_seconds": tk.StringVar(value="30"),
            "timeout_seconds": tk.StringVar(value="5"), "brightness": tk.StringVar(value="150"),
            "rotation": tk.StringVar(value="90°"),
        }
        self.open_network = tk.BooleanVar(value=False)
        self.build()
        self.scan_ports()
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.after(50, self.poll)

    def build(self):
        outer = ttk.Frame(self.root, padding=16)
        outer.pack(fill="both", expand=True)
        connection = ttk.Frame(outer)
        connection.pack(fill="x", pady=(0, 12))
        self.port_box = ttk.Combobox(connection, textvariable=self.port, state="readonly", width=24)
        self.port_box.pack(side="left", fill="x", expand=True)
        ttk.Button(connection, text="刷新串口", command=self.scan_ports).pack(side="left", padx=8)
        self.connect_button = ttk.Button(connection, text="连接设备", command=self.toggle_connection)
        self.connect_button.pack(side="left")
        notebook = ttk.Notebook(outer)
        notebook.pack(fill="both", expand=True)
        self.form = ttk.Frame(notebook, padding=16)
        notebook.add(self.form, text="设备配置")
        self.form.columnconfigure(1, weight=1)
        self.field("Wi-Fi 名称", "ssid", 0)
        self.field("密码（留空保留）", "password", 1, show="•")
        ttk.Checkbutton(self.form, text="开放网络", variable=self.open_network).grid(row=2, column=1, sticky="w", pady=(0, 10))
        self.field("服务器地址", "host", 3)
        self.field("服务器端口", "port", 4)
        self.field("屏幕方向", "rotation", 5, choices=("90°", "270°"))
        advanced = ttk.LabelFrame(self.form, text="查询与显示", padding=12)
        advanced.grid(row=6, column=0, columnspan=2, sticky="ew", pady=(10, 0))
        advanced.columnconfigure(1, weight=1)
        self.field("连接目标（可选）", "connect_host", 0, parent=advanced)
        self.field("握手协议编号", "protocol", 1, parent=advanced)
        self.field("刷新间隔（秒）", "refresh_seconds", 2, parent=advanced)
        self.field("阶段超时（秒）", "timeout_seconds", 3, parent=advanced)
        self.field("屏幕亮度", "brightness", 4, parent=advanced)
        result = ttk.Frame(notebook, padding=12)
        notebook.add(result, text="响应信息")
        ttk.Label(result, textvariable=self.result_summary, wraplength=540).pack(anchor="w", pady=(0, 10))
        text_frame = ttk.Frame(result)
        text_frame.pack(fill="both", expand=True)
        self.raw = tk.Text(text_frame, wrap="word", state="disabled", font=("Consolas", 10))
        self.raw.pack(side="left", fill="both", expand=True)
        scrollbar = ttk.Scrollbar(text_frame, command=self.raw.yview)
        scrollbar.pack(side="right", fill="y")
        self.raw.configure(yscrollcommand=scrollbar.set)
        actions = ttk.Frame(outer)
        actions.pack(fill="x", pady=(12, 8))
        self.read_button = ttk.Button(actions, text="读取配置", command=lambda: self.send("get_config"))
        self.read_button.pack(side="left")
        self.refresh_button = ttk.Button(actions, text="立即查询", command=lambda: self.send("refresh"))
        self.refresh_button.pack(side="left", padx=8)
        self.state_button = ttk.Button(actions, text="读取响应", command=lambda: self.send("get_state"))
        self.state_button.pack(side="left")
        self.save_button = ttk.Button(actions, text="保存到设备", command=self.save)
        self.save_button.pack(side="right")
        ttk.Label(outer, textvariable=self.status, wraplength=600).pack(anchor="w")
        self.update_buttons()

    def field(self, title, key, row, show=None, choices=None, parent=None):
        parent = self.form if parent is None else parent
        ttk.Label(parent, text=title).grid(row=row, column=0, sticky="w", padx=(0, 14), pady=4)
        if choices:
            widget = ttk.Combobox(parent, textvariable=self.values[key], values=choices, state="readonly")
        else:
            widget = ttk.Entry(parent, textvariable=self.values[key], show=show or "")
        widget.grid(row=row, column=1, sticky="ew", pady=4)

    def scan_ports(self):
        self.ports = {f"{port.device} · {port.description}": port.device for port in list_ports.comports()}
        names = list(self.ports)
        self.port_box["values"] = names
        if self.port.get() not in self.ports:
            self.port.set(names[0] if names else "")

    def toggle_connection(self):
        if self.link:
            self.disconnect()
            return
        port = self.ports.get(self.port.get())
        if not port:
            messagebox.showerror("串口", "请选择 StickS3 串口", parent=self.root)
            return
        self.status.set("正在连接设备")
        self.link = SerialLink(port, self.events)
        self.update_buttons()

    def disconnect(self):
        if self.link:
            self.link.stop.set()
        self.link = None
        self.connected = False
        self.config_ready = False
        self.pending = None
        self.values["password"].set("")
        self.status.set("设备连接已断开")
        self.update_buttons()

    def update_buttons(self):
        ready = self.connected and self.pending is None
        self.read_button.configure(state="normal" if ready else "disabled")
        for button in (self.refresh_button, self.state_button, self.save_button):
            button.configure(state="normal" if ready and self.config_ready else "disabled")
        self.connect_button.configure(text="断开设备" if self.link else "连接设备")
        self.port_box.configure(state="disabled" if self.link else "readonly")

    def send(self, command, settings=None):
        if not self.connected or self.pending is not None:
            return
        self.sequence += 1
        message = {"cmd": command, "id": self.sequence}
        if settings is not None:
            message["settings"] = settings
        self.pending = (self.sequence, command, time.monotonic() + 12)
        self.link.commands.put(message)
        self.update_buttons()

    def payload(self):
        settings = {key: value.get() if key == "ssid" else value.get().strip()
                    for key, value in self.values.items() if key != "password"}
        if not 1 <= len(settings["ssid"].encode("utf-8")) <= 32:
            raise ValueError("Wi-Fi 名称必须为 1~32 字节")
        for key in ("host", "connect_host"):
            text = settings[key]
            if key == "connect_host" and not text:
                continue
            if not re.fullmatch(r"[A-Za-z0-9_.-]{1,253}", text):
                raise ValueError("服务器地址应填写 IPv4 地址或域名，端口填写在端口字段")
        limits = {"port": (1, 65535), "protocol": (0, 2147483647), "refresh_seconds": (10, 3600),
                  "timeout_seconds": (1, 30), "brightness": (20, 255)}
        titles = {"port": "服务器端口", "protocol": "握手协议编号", "refresh_seconds": "刷新间隔",
                  "timeout_seconds": "阶段超时", "brightness": "屏幕亮度"}
        for key, (low, high) in limits.items():
            if not settings[key].isdecimal() or not low <= int(settings[key]) <= high:
                raise ValueError(f"{titles[key]}必须为 {low}~{high} 的整数")
            settings[key] = int(settings[key])
        settings["rotation"] = int(settings["rotation"].removesuffix("°"))
        if settings["rotation"] not in (90, 270):
            raise ValueError("屏幕方向必须为 90° 或 270°")
        password = self.values["password"].get()
        if self.open_network.get():
            settings["password"] = ""
        elif password:
            if not 8 <= len(password.encode("utf-8")) <= 63:
                raise ValueError("Wi-Fi 密码必须为 8~63 字节")
            settings["password"] = password
        elif settings["ssid"] != self.saved_ssid:
            raise ValueError("更换 Wi-Fi 时需要填写密码或选择开放网络")
        return settings

    def save(self):
        try:
            settings = self.payload()
        except ValueError as error:
            messagebox.showerror("配置", str(error), parent=self.root)
            return
        self.send("configure", settings)
        self.status.set("正在保存配置")

    def apply_config(self, config):
        for key, variable in self.values.items():
            if key == "password":
                variable.set("")
            else:
                variable.set(str(config[key]) + ("°" if key == "rotation" else ""))
        self.password_set = bool(config["password_set"])
        self.saved_ssid = config["ssid"]
        self.open_network.set(bool(config["ssid"]) and not self.password_set)
        self.config_ready = True

    def handle_message(self, message):
        if message.get("type") == "reply":
            if "id" not in message:
                self.status.set(message.get("error", "串口命令响应无效"))
                return
            if not self.pending or message["id"] != self.pending[0]:
                return
            command = self.pending[1]
            self.pending = None
            if not message.get("ok"):
                self.status.set(message.get("error", "设备操作失败"))
            else:
                if "config" in message:
                    self.apply_config(message["config"])
                self.status.set("配置已保存到设备" if command == "configure" else "设备操作完成")
            self.update_buttons()
        elif message.get("type") == "state":
            state = "Wi-Fi 已连接" if message["wifi"] else "Wi-Fi 正在连接"
            if not message["configured"]:
                state = "等待设备配置"
            if message["busy"]:
                state += " · 查询中"
            result = message.get("result")
            if result:
                self.result_summary.set(
                    f"{result['version']}  |  {result['online']} / {result['max']} 人  |  "
                    f"DNS {result['dns_ms']} ms · TCP {result['tcp_ms']} ms · "
                    f"状态 {result['status_ms']} ms · Pong {result['pong_ms']} ms"
                    if result["status_received"] else f"{result['stage']}：{result['error']}"
                )
                if result["error"]:
                    state += f" · {result['error']}"
                if "raw_json" in result:
                    self.raw.configure(state="normal")
                    self.raw.delete("1.0", "end")
                    self.raw.insert("1.0", result["raw_json"])
                    self.raw.configure(state="disabled")
            else:
                self.result_summary.set("等待设备响应")
                self.raw.configure(state="normal")
                self.raw.delete("1.0", "end")
                self.raw.configure(state="disabled")
            self.status.set(state)

    def poll(self):
        while not self.events.empty():
            kind, value = self.events.get_nowait()
            source = value if kind in ("connected", "disconnected") else value[0]
            if source is not self.link:
                continue
            if kind == "connected":
                self.connected = True
                self.status.set("设备已连接")
                self.send("get_config")
            elif kind == "message":
                try:
                    self.handle_message(value[1])
                except (KeyError, TypeError, ValueError) as error:
                    self.disconnect()
                    self.status.set(f"设备响应格式无效：{error}")
            elif kind == "log":
                self.status.set("设备输出：" + value[1][:160])
            elif kind == "error":
                self.disconnect()
                self.status.set(value[1])
            elif kind == "disconnected":
                self.disconnect()
        if self.pending and time.monotonic() > self.pending[2]:
            self.pending = None
            self.status.set("设备响应超时，请检查固件和 USB 连接")
            self.update_buttons()
        self.root.after(50, self.poll)

    def close(self):
        if self.link:
            self.link.stop.set()
        self.root.destroy()


def main():
    root = tk.Tk()
    App(root)
    root.mainloop()
