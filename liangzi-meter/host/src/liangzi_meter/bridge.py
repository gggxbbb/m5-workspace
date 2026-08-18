"""liangzi-meter 上位机：串口桥（JSON Lines 协议）。"""
from __future__ import annotations

import json
import threading

from PyQt6.QtCore import QObject, pyqtSignal
from serial import Serial
from serial.tools import list_ports as serial_list_ports

BAUD = 115200


def list_ports() -> list[str]:
    return [p.device for p in serial_list_ports.comports()]


class SerialBridge(QObject):
    log = pyqtSignal(str)
    message = pyqtSignal(dict)
    connected = pyqtSignal(bool)
    error = pyqtSignal(str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._ser: Serial | None = None
        self._rx_thread: threading.Thread | None = None
        self._lock = threading.Lock()
        self._buf = b""

    @property
    def is_open(self) -> bool:
        return self._ser is not None and self._ser.is_open

    def connect_port(self, port: str) -> bool:
        try:
            ser = Serial(port, BAUD, timeout=0.1)
        except Exception as exc:  # noqa: BLE001
            self.error.emit(f"打开 {port} 失败: {exc}")
            return False
        self._ser = ser
        self._buf = b""
        self._rx_thread = threading.Thread(target=self._read_loop, daemon=True)
        self._rx_thread.start()
        self.connected.emit(True)
        self.log.emit(f"已连接 {port} @ {BAUD}")
        self.send_ping()
        return True

    def disconnect(self) -> None:
        ser, self._ser = self._ser, None
        if ser is not None and ser.is_open:
            try:
                ser.close()
            except Exception:  # noqa: BLE001
                pass
        self.connected.emit(False)
        self.log.emit("已断开")

    def _read_loop(self) -> None:
        while True:
            ser = self._ser
            if ser is None or not ser.is_open:
                return
            try:
                data = ser.read(256)
            except Exception as exc:  # noqa: BLE001
                self.error.emit(f"串口读取错误: {exc}")
                return
            if not data:
                continue
            self._buf += data
            while b"\n" in self._buf:
                line, self._buf = self._buf.split(b"\n", 1)
                line = line.strip()
                if not line:
                    continue
                try:
                    msg = json.loads(line.decode("utf-8"))
                except (ValueError, UnicodeDecodeError):
                    self.log.emit(f"[固件] 非 JSON 行: {line[:80]!r}")
                    continue
                if isinstance(msg, dict):
                    self.message.emit(msg)

    def _send(self, doc: dict) -> bool:
        ser = self._ser
        if ser is None or not ser.is_open:
            self.error.emit("未连接设备")
            return False
        try:
            payload = (json.dumps(doc, ensure_ascii=False) + "\n").encode("utf-8")
            with self._lock:
                ser.write(payload)
            return True
        except Exception as exc:  # noqa: BLE001
            self.error.emit(f"串口写入失败: {exc}")
            return False

    def send_ping(self) -> None:
        self._send({"type": "ping"})

    def send_get_state(self) -> None:
        self._send({"type": "get_state"})

    def send_config(self, cfg: dict) -> bool:
        doc = dict(cfg)
        doc["type"] = "config"
        if self._send(doc):
            self.log.emit("已下发配置")
            return True
        return False
