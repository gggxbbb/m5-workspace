"""liangzi-meter 上位机：配置存储。"""
from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

CONFIG_PATH = Path(__file__).resolve().parents[2] / "config.json"

DEFAULT_NTP = "ntp.aliyun.com"
# 官方默认峰谷（北京时间，2026-08-17 生效）：高峰 9:00-12:00 / 14:00-18:00
DEFAULT_PEAK_RANGES: list[tuple[str, str]] = [("09:00", "12:00"), ("14:00", "18:00")]
# 余额告警阈值（元）：余额低于该值，设备与上位机显示红色
DEFAULT_BALANCE_WARN = 10.0


@dataclass
class Config:
    wifi_ssid: str = ""
    wifi_password: str = ""
    ntp: str = DEFAULT_NTP
    api_key: str = ""
    peak_override: bool = False
    peak_ranges: list[tuple[str, str]] = field(
        default_factory=lambda: list(DEFAULT_PEAK_RANGES)
    )
    balance_warn: float = DEFAULT_BALANCE_WARN
    # 提示音功能总开关（默认关，见 ADR-0004）
    alert_enabled: bool = False
    # 屏幕方向：0=竖屏正常 1=顺时针90°(横屏) 2=180° 3=逆时针90°(横屏)（见 ADR-0005）
    screen_rotation: int = 0
    # 重力感应旋屏开关（默认关，见 ADR-0005）
    auto_rotate: bool = False

    def to_message(self) -> dict:
        """转换为串口 config 消息（不含 type，由发送方补全）。"""
        msg: dict = {
            "wifi": {"ssid": self.wifi_ssid, "password": self.wifi_password},
            "ntp": self.ntp,
            "api_key": self.api_key,
            "balance_warn": self.balance_warn,
            "alert_enabled": self.alert_enabled,
            "screen_rotation": self.screen_rotation,
            "auto_rotate": self.auto_rotate,
        }
        if self.peak_override:
            msg["peak_ranges"] = [
                {"start": s, "end": e}
                for s, e in self.peak_ranges
                if s and e
            ]
        return msg


def load_config() -> Config:
    if not CONFIG_PATH.exists():
        return Config()
    try:
        data = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        cfg = Config()
        cfg.wifi_ssid = str(data.get("wifi_ssid", ""))
        cfg.wifi_password = str(data.get("wifi_password", ""))
        cfg.ntp = str(data.get("ntp", DEFAULT_NTP)) or DEFAULT_NTP
        cfg.api_key = str(data.get("api_key", ""))
        cfg.peak_override = bool(data.get("peak_override", False))
        ranges = data.get("peak_ranges") or []
        cfg.peak_ranges = [
            (str(s), str(e)) for s, e in ranges[:4]
        ] or list(DEFAULT_PEAK_RANGES)
        try:
            cfg.balance_warn = float(data.get("balance_warn", DEFAULT_BALANCE_WARN))
        except (TypeError, ValueError):
            cfg.balance_warn = DEFAULT_BALANCE_WARN
        cfg.alert_enabled = bool(data.get("alert_enabled", False))
        try:
            rot = int(data.get("screen_rotation", 0))
        except (TypeError, ValueError):
            rot = 0
        cfg.screen_rotation = rot if 0 <= rot <= 3 else 0
        # 只认 JSON 布尔 true，避免手动编辑时字符串 "yes"/"1" 被 bool() 误判
        cfg.auto_rotate = data.get("auto_rotate", False) is True
        return cfg
    except (OSError, ValueError, TypeError):
        return Config()


def save_config(cfg: Config) -> None:
    CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    data = {
        "wifi_ssid": cfg.wifi_ssid,
        "wifi_password": cfg.wifi_password,
        "ntp": cfg.ntp,
        "api_key": cfg.api_key,
        "peak_override": cfg.peak_override,
        "peak_ranges": [list(r) for r in cfg.peak_ranges],
        "balance_warn": cfg.balance_warn,
        "alert_enabled": cfg.alert_enabled,
        "screen_rotation": cfg.screen_rotation,
        "auto_rotate": cfg.auto_rotate,
    }
    CONFIG_PATH.write_text(
        json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8"
    )
