import argparse
import json
import time

import serial


def request(device, command, identifier):
    encoded = json.dumps({"cmd": command, "id": identifier}).encode("utf-8") + b"\n"
    if device.write(encoded) != len(encoded):
        raise RuntimeError("串口命令发送中断")
    deadline = time.monotonic() + 12
    state = None
    while time.monotonic() < deadline:
        line = device.read_until(b"\n", 524288)
        if not line.strip():
            continue
        try:
            message = json.loads(line)
        except (json.JSONDecodeError, UnicodeDecodeError):
            print("设备输出：" + line.decode("utf-8", errors="replace").strip())
            continue
        if message.get("type") == "state":
            state = message
        elif message.get("type") == "reply" and message.get("id") == identifier:
            if not message.get("ok"):
                raise RuntimeError(message.get("error", "设备操作失败"))
            return message, state
    raise TimeoutError(f"{command} 响应超时")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port")
    args = parser.parse_args()
    with serial.Serial(args.port, 115200, timeout=0.2, write_timeout=2) as device:
        reply, _ = request(device, "get_config", 1)
        config = reply["config"]
        assert config["rotation"] in (90, 270)
        assert "password" not in config
        assert isinstance(config["password_set"], bool)
        _, state = request(device, "get_state", 2)
        assert state is not None
        assert state["configured"] == bool(config["ssid"] and config["host"])
        assert state["rotation"] in (90, 270)
        print(json.dumps({"usb": "PASS", "port": args.port, "rotation": state["rotation"],
                          "configured": state["configured"], "wifi": state["wifi"], "busy": state["busy"]}))


if __name__ == "__main__":
    main()
