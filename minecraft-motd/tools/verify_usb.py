import argparse
import json
import time

import serial


def request(device, command, identifier):
    encoded = json.dumps({"cmd": command, "id": identifier}).encode("utf-8") + b"\n"
    if device.write(encoded) != len(encoded):
        raise RuntimeError("Serial command transmission interrupted")
    deadline = time.monotonic() + 12
    state = None
    while time.monotonic() < deadline:
        line = device.read_until(b"\n", 524288)
        if not line.strip():
            continue
        try:
            message = json.loads(line)
        except (json.JSONDecodeError, UnicodeDecodeError):
            print("Device output: " + line.decode("utf-8", errors="replace").strip()[:160])
            continue
        if message.get("type") == "state":
            state = message
        elif message.get("type") == "reply" and message.get("id") == identifier:
            if not message.get("ok"):
                raise RuntimeError(message.get("error", "Device operation failed"))
            return message, state
    raise TimeoutError(f"{command} response timed out")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port")
    parser.add_argument("--wait-query", action="store_true")
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
        if args.wait_query:
            deadline = time.monotonic() + 35
            identifier = 3
            while not state.get("result", {}).get("complete"):
                if time.monotonic() >= deadline:
                    raise TimeoutError("Device query did not complete")
                time.sleep(1)
                _, state = request(device, "get_state", identifier)
                identifier += 1
            assert state["result"]["stage"] == "Done"
        print(json.dumps({"usb": "PASS", "port": args.port, "rotation": state["rotation"],
                          "configured": state["configured"], "wifi": state["wifi"], "busy": state["busy"],
                          "query_complete": state.get("result", {}).get("complete", False)}))


if __name__ == "__main__":
    main()
