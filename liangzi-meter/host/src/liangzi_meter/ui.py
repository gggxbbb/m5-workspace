"""liangzi-meter 上位机：主窗口。"""
from __future__ import annotations

from PyQt6.QtCore import Qt, QTime
from PyQt6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QPlainTextEdit,
    QPushButton,
    QTimeEdit,
    QVBoxLayout,
    QWidget,
)

from .bridge import SerialBridge, list_ports
from .config import Config, DEFAULT_NTP, DEFAULT_BALANCE_WARN, load_config, save_config

CLR_PEAK = "#e0a83e"
CLR_OFF = "#2ea86b"
CLR_DIM = "#8a8f98"
CLR_WARN = "#e0483e"


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("liangzi-meter 上位机")
        self.resize(460, 720)

        self.bridge = SerialBridge(self)
        self._auto_pushed = False

        self._build_ui()
        self._wire_signals()

        # 加载本地已保存配置到表单
        self.cfg = load_config()
        self._cfg_to_form(self.cfg)

    # ---------- UI 构建 ----------

    def _build_ui(self) -> None:
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)

        # --- 串口区 ---
        ser_box = QGroupBox("串口")
        ser_row = QHBoxLayout(ser_box)
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(140)
        self.refresh_btn = QPushButton("刷新")
        self.connect_btn = QPushButton("连接")
        self.ser_status = QLabel("未连接")
        self.ser_status.setStyleSheet(f"color: {CLR_DIM};")
        ser_row.addWidget(self.port_combo)
        ser_row.addWidget(self.refresh_btn)
        ser_row.addWidget(self.connect_btn)
        ser_row.addWidget(self.ser_status, 1)
        root.addWidget(ser_box)

        # --- 配置区 ---
        cfg_box = QGroupBox("配置（下发后固件持久化）")
        form = QFormLayout(cfg_box)
        self.ssid_edit = QLineEdit()
        self.ssid_edit.setPlaceholderText("WiFi SSID")
        self.pass_edit = QLineEdit()
        self.pass_edit.setEchoMode(QLineEdit.EchoMode.Password)
        self.ntp_edit = QLineEdit(DEFAULT_NTP)
        self.key_edit = QLineEdit()
        self.key_edit.setEchoMode(QLineEdit.EchoMode.Password)
        self.key_edit.setPlaceholderText("sk-...")
        self.key_show = QCheckBox("显示")
        key_row = QHBoxLayout()
        key_row.addWidget(self.key_edit, 1)
        key_row.addWidget(self.key_show)

        self.warn_spin = QDoubleSpinBox()
        self.warn_spin.setRange(0.0, 999999.0)
        self.warn_spin.setDecimals(2)
        self.warn_spin.setSingleStep(1.0)
        self.warn_spin.setValue(DEFAULT_BALANCE_WARN)
        self.warn_spin.setSuffix(" 元")

        form.addRow("WiFi SSID", self.ssid_edit)
        form.addRow("WiFi 密码", self.pass_edit)
        form.addRow("NTP 服务器", self.ntp_edit)
        form.addRow("API Key", key_row)
        form.addRow("余额告警阈值", self.warn_spin)

        self.alert_check = QCheckBox(
            "提示音功能（进入高峰提示 + 高峰余额减少持续告警，默认关）"
        )
        form.addRow("", self.alert_check)

        self.peak_check = QCheckBox("覆盖官方峰谷时段（官方默认 09:00-12:00 / 14:00-18:00）")
        form.addRow("", self.peak_check)
        peak_grid = QVBoxLayout()
        for label, attrs in (("高峰时段 1", "r1"), ("高峰时段 2", "r2")):
            row = QHBoxLayout()
            s = QTimeEdit(QTime(9, 0) if attrs == "r1" else QTime(14, 0))
            e = QTimeEdit(QTime(12, 0) if attrs == "r1" else QTime(18, 0))
            s.setDisplayFormat("HH:mm")
            e.setDisplayFormat("HH:mm")
            row.addWidget(QLabel(label))
            row.addWidget(s)
            row.addWidget(QLabel("至"))
            row.addWidget(e)
            row.addStretch(1)
            setattr(self, f"peak_{attrs}_start", s)
            setattr(self, f"peak_{attrs}_end", e)
            peak_grid.addLayout(row)
        form.addRow("", peak_grid)
        root.addWidget(cfg_box)

        # --- 操作区 ---
        act_box = QGroupBox("操作")
        act_row = QHBoxLayout(act_box)
        self.send_btn = QPushButton("下发配置")
        self.state_btn = QPushButton("读取状态")
        act_row.addWidget(self.send_btn)
        act_row.addWidget(self.state_btn)
        act_row.addStretch(1)
        root.addWidget(act_box)

        # --- 设备状态区 ---
        dev_box = QGroupBox("设备状态")
        dev_form = QFormLayout(dev_box)
        self.st_time = QLabel("-")
        self.st_phase = QLabel("-")
        self.st_balance = QLabel("-")
        self.st_battery = QLabel("-")
        self.st_wifi = QLabel("-")
        self.st_fw = QLabel("-")
        dev_form.addRow("时间", self.st_time)
        dev_form.addRow("峰谷", self.st_phase)
        dev_form.addRow("余额", self.st_balance)
        dev_form.addRow("电量", self.st_battery)
        dev_form.addRow("WiFi", self.st_wifi)
        dev_form.addRow("固件", self.st_fw)
        root.addWidget(dev_box)

        # --- 日志区 ---
        self.log_view = QPlainTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setMaximumBlockCount(2000)
        root.addWidget(self.log_view, 1)

    def _wire_signals(self) -> None:
        self.refresh_btn.clicked.connect(self._refresh_ports)
        self.connect_btn.clicked.connect(self._toggle_connect)
        self.key_show.toggled.connect(
            lambda on: self.key_edit.setEchoMode(
                QLineEdit.EchoMode.Normal if on else QLineEdit.EchoMode.Password
            )
        )
        self.send_btn.clicked.connect(self._send_config)
        self.state_btn.clicked.connect(self.bridge.send_get_state)
        self.bridge.log.connect(self._log)
        self.bridge.error.connect(self._log)
        self.bridge.message.connect(self._on_message)
        self.bridge.connected.connect(self._on_connected)
        self._refresh_ports()

    # ---------- 行为 ----------

    def _log(self, text: str) -> None:
        self.log_view.appendPlainText(text)

    def _refresh_ports(self) -> None:
        current = self.port_combo.currentText()
        self.port_combo.clear()
        ports = list_ports()
        self.port_combo.addItems(ports)
        if current in ports:
            self.port_combo.setCurrentText(current)
        self.log_view.appendPlainText(
            f"发现串口: {', '.join(ports) if ports else '（无）'}"
        )

    def _toggle_connect(self) -> None:
        if self.bridge.is_open:
            self.bridge.disconnect()
        else:
            port = self.port_combo.currentText()
            if not port:
                self._log("请先选择串口")
                return
            self._auto_pushed = False
            self.bridge.connect_port(port)

    def _on_connected(self, ok: bool) -> None:
        if ok:
            self.connect_btn.setText("断开")
            self.ser_status.setText("已连接")
            self.ser_status.setStyleSheet("color: #2ea86b;")
        else:
            self.connect_btn.setText("连接")
            self.ser_status.setText("未连接")
            self.ser_status.setStyleSheet(f"color: {CLR_DIM};")

    # ---------- 配置表单 ----------

    def _cfg_to_form(self, cfg: Config) -> None:
        self.ssid_edit.setText(cfg.wifi_ssid)
        self.pass_edit.setText(cfg.wifi_password)
        self.ntp_edit.setText(cfg.ntp or DEFAULT_NTP)
        self.key_edit.setText(cfg.api_key)
        self.warn_spin.setValue(cfg.balance_warn)
        self.alert_check.setChecked(cfg.alert_enabled)
        self.peak_check.setChecked(cfg.peak_override)
        ranges = list(cfg.peak_ranges or [])
        defaults = [("09:00", "12:00"), ("14:00", "18:00")]
        for i, (start, end) in enumerate(defaults):
            if i < len(ranges):
                start, end = ranges[i]
            s = QTime.fromString(start, "HH:mm")
            e = QTime.fromString(end, "HH:mm")
            if s.isValid():
                self._peak_time(i, "start").setTime(s)
            if e.isValid():
                self._peak_time(i, "end").setTime(e)

    def _peak_time(self, index: int, kind: str) -> QTimeEdit:
        return getattr(self, f"peak_{'r1' if index == 0 else 'r2'}_{kind}")

    def _form_to_cfg(self) -> Config:
        cfg = Config()
        cfg.wifi_ssid = self.ssid_edit.text().strip()
        cfg.wifi_password = self.pass_edit.text()
        cfg.ntp = self.ntp_edit.text().strip() or DEFAULT_NTP
        cfg.api_key = self.key_edit.text().strip()
        cfg.balance_warn = self.warn_spin.value()
        cfg.alert_enabled = self.alert_check.isChecked()
        cfg.peak_override = self.peak_check.isChecked()
        ranges = []
        for i in range(2):
            s = self._peak_time(i, "start").time()
            e = self._peak_time(i, "end").time()
            ranges.append((f"{s.hour():02d}:{s.minute():02d}", f"{e.hour():02d}:{e.minute():02d}"))
        cfg.peak_ranges = ranges
        return cfg

    def _send_config(self) -> None:
        cfg = self._form_to_cfg()
        save_config(cfg)
        self._log("配置已保存到 config.json")
        if not self.bridge.send_config(cfg.to_message()):
            self._log("下发失败：设备未连接")

    # ---------- 消息处理 ----------

    def _on_message(self, msg: dict) -> None:
        mtype = msg.get("type")
        if mtype == "hello":
            fw = msg.get("fw", "?")
            configured = "已配置" if msg.get("configured") else "未配置"
            self._log(f"设备就绪：{msg.get('model', '?')} 固件 v{fw}（{configured}）")
            self.st_fw.setText(f"v{fw}")
            # 自动下发本地已保存配置（每次连接仅一次）
            if not self._auto_pushed and load_config().wifi_ssid:
                self._auto_pushed = True
                saved = load_config()
                self._log("检测到已保存配置，自动下发…")
                self.bridge.send_config(saved.to_message())
        elif mtype == "ack":
            ok = msg.get("ok")
            text = msg.get("msg", "")
            self._log(f"[固件] ack {'✓' if ok else '✗'} {text}")
        elif mtype == "state":
            self._apply_state(msg)

    def _apply_state(self, msg: dict) -> None:
        self.st_time.setText(str(msg.get("time", "-")))
        phase = msg.get("phase")
        if phase == "peak":
            self.st_phase.setText("高峰期 梁文峰")
            self.st_phase.setStyleSheet(f"color: {CLR_PEAK}; font-weight: bold;")
        elif phase == "offpeak":
            self.st_phase.setText("非高峰期 梁文谷")
            self.st_phase.setStyleSheet(f"color: {CLR_OFF}; font-weight: bold;")
        else:
            self.st_phase.setText("时间未同步")

        bal = msg.get("balance")
        if isinstance(bal, (int, float)) and bal >= 0:
            suffix = "（旧）" if msg.get("balance_expired") else ""
            self.st_balance.setText(f"¥{bal:.2f}{suffix}")
            if bal < self.warn_spin.value():
                self.st_balance.setStyleSheet(f"color: {CLR_WARN}; font-weight: bold;")
            else:
                self.st_balance.setStyleSheet("")
        else:
            self.st_balance.setText("--")

        batt = msg.get("battery")
        self.st_battery.setText(f"{batt}%" if isinstance(batt, int) and batt >= 0 else "-")
        wifi = msg.get("wifi")
        ssid = msg.get("ssid", "")
        self.st_wifi.setText(
            f"在线（{ssid}）" if wifi else ("未配置" if not ssid else "断线")
        )
