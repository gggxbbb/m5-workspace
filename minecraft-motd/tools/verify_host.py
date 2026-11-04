import argparse
import json
import re
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
    assert re.search(r"[\u3400-\u9fff]", response.stderr.decode("utf-8")) is None
    return json.loads(response.stdout) if expected else None


def verify_english_widgets(widget):
    if "text" in widget.keys():
        assert re.search(r"[\u3400-\u9fff]", str(widget.cget("text"))) is None
    if widget.winfo_class() == "TNotebook":
        for tab in widget.tabs():
            assert re.search(r"[\u3400-\u9fff]", widget.tab(tab, "text")) is None
    for child in widget.winfo_children():
        verify_english_widgets(child)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("port", type=int)
    args = parser.parse_args()
    root = tk.Tk()
    root.withdraw()
    app = App(root)
    root.update_idletasks()
    verify_english_widgets(root)
    assert app.status.get() == "Select the StickS3 serial port"
    assert app.result_summary.get() == "Waiting for device response"
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
        raise AssertionError("The desktop app must reject invalid ports")
    assert app.read_button.cget("text") == "Read settings"
    assert app.save_button.cget("text") == "Save to device"
    assert app.root.winfo_exists()
    root.destroy()
    print("PASS: English UI, Tk initialization, both rotations, settings validation, password protection and 8 boundary checks")


if __name__ == "__main__":
    main()
