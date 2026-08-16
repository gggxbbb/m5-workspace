#!/usr/bin/env python3
"""W96P USB HID 主机端工具 — 读状态流 / 发控制命令

对应固件: tools/usb-hid/usb-hid.ino (USBHIDVendor, usage page 0xFF00)
依赖: hidapi (pip install hidapi; Windows 自带 hid.dll, 无需额外驱动)

用法:
  uv run --with hidapi python hid_client.py --list              # 枚举设备
  uv run --with hidapi python hid_client.py --monitor           # 只读状态流(500ms/条)
  uv run --with hidapi python hid_client.py --cmd "SPD 65"      # 单条命令
  uv run --with hidapi python hid_client.py                     # 交互模式(输入命令回车)

协议 (与固件一致, 可读文本):
  IN  (设备→主机): "S:65 G:3 P:1 B:4.00 M:129 T:0\\r\\n"   每 500ms 一条
  OUT (主机→设备): "SPD 65" / "PWR 1" / "GEAR 3" / "NAT 1" / "TMR 60" / "LGT 4"
  响应: "OK <CMD> <VAL>" / "ERR ..."
"""

import argparse
import sys
import threading

VID = 0x303A               # Espressif
VENDOR_USAGE_PAGE = 0xFF00  # vendor-defined


def find_devices():
    import hid
    devs = [
        d for d in hid.enumerate()
        if d["vendor_id"] == VID and d.get("usage_page") == VENDOR_USAGE_PAGE
    ]
    return devs


def pick_device(devs):
    if not devs:
        sys.exit("no vendor HID device found (VID=0x%04X usage=0x%02X); "
                 "is the firmware running?" % (VID, VENDOR_USAGE_PAGE))
    if len(devs) > 1:
        print("multiple devices, using first:")
    for i, d in enumerate(devs[:3]):
        print("  [%d] VID=%04X PID=%04X path=%s" % (
            i, d["vendor_id"], d["product_id"], d["path"]))
    return devs[0]


HID_REPORT_ID_VENDOR = 0x06   # USBHIDVendor 的 IN/OUT/FEATURE 共用 report id 6


def send_line(dev, text):
    # 命令走 FEATURE report (report id 6):
    # arduino-esp32 把带 report id 的 OUTPUT 路由到 feature 分支, write() 发的
    # OUT report 固件收不到 (USBHIDVendor 的 _onOutput 不被调用)。见 usb-hid.ino。
    dev.send_feature_report(bytes([HID_REPORT_ID_VENDOR]) + text.encode("utf-8") + b"\r\n")


def read_loop(dev, stop):
    while not stop.is_set():
        data = dev.read(64, timeout_ms=200)   # cffi hid: 返回 int 列表, 空列表=超时
        if not data:
            continue
        payload = bytes(data[1:])   # 首字节恒为 report id (USBHIDVendor = 0x06), 去掉
        try:
            sys.stdout.write(payload.decode("utf-8", "replace"))
            sys.stdout.flush()
        except OSError:
            return


def interactive(dev):
    stop = threading.Event()
    t = threading.Thread(target=read_loop, args=(dev, stop), daemon=True)
    t.start()
    print("interactive: type a command (SPD/PWR/GEAR/NAT/TMR/LGT) or Ctrl-C to quit")
    try:
        while True:
            line = input("> ").strip()
            if not line:
                continue
            send_line(dev, line)
    except (KeyboardInterrupt, EOFError):
        pass
    finally:
        stop.set()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true", help="list matching HID devices and exit")
    ap.add_argument("--monitor", action="store_true", help="read-only status stream")
    ap.add_argument("--cmd", metavar='"SPD 65"', help="send one command then exit")
    args = ap.parse_args()

    devs = find_devices()
    if args.list:
        if not devs:
            print("no vendor HID device found")
            return
        for d in devs:
            print("VID=%04X PID=%04X usage_page=0x%02X usage=0x%02X path=%s" % (
                d["vendor_id"], d["product_id"],
                d.get("usage_page", 0), d.get("usage", 0), d["path"]))
        return

    info = pick_device(devs)
    import hid
    dev = hid.device()
    dev.open(info["vendor_id"], info["product_id"])

    if args.cmd:
        send_line(dev, args.cmd)
        stop = threading.Event()
        read_loop(dev, stop)   # 收响应直到超时退出
        return

    if args.monitor:
        stop = threading.Event()
        try:
            read_loop(dev, stop)
        except KeyboardInterrupt:
            pass
        return

    interactive(dev)


if __name__ == "__main__":
    main()
