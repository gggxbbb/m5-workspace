import argparse
import json
from pathlib import Path
import subprocess
import sys
import tkinter as tk

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host" / "src"))

from motd_host.app import App


def validate(executable, settings, expected):
    response = subprocess.run(
        [str(executable), "--validate-config"],
        input=json.dumps(settings, ensure_ascii=False).encode("utf-8") + b"\n",
        capture_output=True,
        check=False,
    )
    assert (response.returncode == 0) == expected, response.stderr.decode("utf-8", errors="replace")
    return json.loads(response.stdout) if expected else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("port", type=int)
    args = parser.parse_args()
    root = tk.Tk()
    root.withdraw()
    app = App(root)
    root.update_idletasks()
    executable = Path(__file__).resolve().parents[1] / ".build" / "native" / "native-probe.exe"
    app.values["ssid"].set(" MOTD 验证 ")
    app.values["host"].set(args.host)
    app.values["port"].set(str(args.port))
    app.open_network.set(True)
    for angle in (90, 270):
        app.values["rotation"].set(f"{angle}°")
        settings = app.payload()
        output = validate(executable, settings, True)
        assert output["rotation"] == angle
        assert output["ssid"] == settings["ssid"]
        assert output["host"] == args.host and output["port"] == args.port
        assert "password" not in output and not output["password_set"]
    settings = app.payload()
    for invalid in ({"port": 0}, {"port": 65536}, {"rotation": 180}, {"refresh_seconds": 9},
                    {"timeout_seconds": 31}, {"host": ""}, {"password": "short"}, {"brightness": 19}):
        validate(executable, settings | invalid, False)
    app.open_network.set(False)
    app.values["password"].set("validation-password")
    output = validate(executable, app.payload(), True)
    assert output["password_set"] and "password" not in output
    app.values["port"].set("0")
    try:
        app.payload()
    except ValueError:
        pass
    else:
        raise AssertionError("上位机必须拒绝无效端口")
    assert app.root.winfo_exists()
    root.destroy()
    print("PASS: 真实 Tk 窗口初始化、两个横屏方向、跨语言配置校验、密码保护及 8 项边界检查")


if __name__ == "__main__":
    main()
