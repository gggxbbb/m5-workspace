// usb-hid.ino — W96P 风扇 USB HID 双向通道 (M5StickS3)
//
// 把 USB-C 变成 vendor-defined HID 设备 (USBHIDVendor, usage page 0xFF00):
//   IN  (设备→主机): 每 500ms 推送 MOCK 状态快照文本
//   OUT (主机→设备): 命令 "SPD 65" / "PWR 1" / "GEAR 3" / "NAT 1" / "TMR 60" / "LGT 4"
//   响应: "OK <CMD> <VAL>" / "ERR ..."
//
// 相比 VFS(虚拟U盘)方案: 无 FAT 缓存问题、实时双向、实现简单(Stream 接口)。
// 设计决策记录: w96p-remote/docs/usb-vfs-design.md (VFS 已标注弃用方向)
//
// 编译(必须 USBMode=default 切 OTG, 否则 #error):
//   arduino-cli compile --libraries ./w96p-remote/lib \
//     --fqbn m5stack:esp32:m5stack_sticks3:USBMode=default,CDCOnBoot=cdc \
//     w96p-remote/tools/usb-hid
//
// 主机端工具: tools/usb-hid/host/hid_client.py
// 上真机后: 把 MOCK 状态替换为 w96p::Snapshot, 命令映射到 setSpeed/setPower 等。

#include <Arduino.h>
#include <cstring>
#include <cstdio>

#include "USB.h"
#include "USBHIDVendor.h"

#if !defined(ARDUINO_USB_MODE) || ARDUINO_USB_MODE == 1
#error "usb-hid requires USB-OTG (TinyUSB): compile with USBMode=default"
#endif

USBHIDVendor Vendor;

// ============================== MOCK 状态 ==============================
// 真机替换: 由 w96p::Client 快照驱动, 命令映射到 client API。
static char stateBuf[128];

static void buildStateText() {
  uint32_t t = millis() / 1000;
  int  speed = (t / 5) % 101;                  // 0-100 循环
  int  gear  = (t / 3) % 4 + 1;                // 1-4
  bool on    = (t / 7) % 2 == 0;
  float bat  = 4.05f - 0.05f * ((t / 11) % 10);
  int  mot   = on ? (120 + speed * 3) : 0;
  snprintf(stateBuf, sizeof(stateBuf),
           "S:%d G:%d P:%d B:%.2f M:%d T:0\r\n",
           speed, gear, on ? 1 : 0, bat, mot);
}

// ============================== 命令解析 ==============================
// 协议(可读文本): "CMD VAL" → 响应 "OK CMD VAL" / "ERR ..."
static void handleCommand(char *line) {
  char cmd[8];
  int  val = 0;
  if (sscanf(line, "%7s %d", cmd, &val) != 2) {
    Vendor.println("ERR bad cmd");
    return;
  }
  bool ok = true;
  if      (strcmp(cmd, "SPD") == 0) { /* 真机: cli.setSpeed(val) */ }
  else if (strcmp(cmd, "PWR") == 0) { /* 真机: cli.setPower(val) */ }
  else if (strcmp(cmd, "GEAR") == 0) { /* 真机: cli.setPower(val) */ }
  else if (strcmp(cmd, "NAT") == 0)  { /* 真机: cli.setNatureWind(val) */ }
  else if (strcmp(cmd, "TMR") == 0)  { /* 真机: cli.setTimerMinutes(val) */ }
  else if (strcmp(cmd, "LGT") == 0)  { /* 真机: cli.setLight(val) */ }
  else ok = false;

  if (ok) Vendor.printf("OK %s %d\r\n", cmd, val);
  else    Vendor.println("ERR unknown");
}

// ============================== 主程序 ==============================
void setup() {
  Serial.begin(115200);
  delay(200);

  Vendor.begin();
  USB.begin();

  Serial.println("[hid] USB HID ready - vendor channel (usage page 0xFF00)");
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 500) {
    last = millis();
    buildStateText();
    Vendor.print(stateBuf);                       // IN report: 推状态
    Serial.printf("[hid] TX: %s", stateBuf);
  }

  // OUT report: 收命令(按行解析)
  static char line[64];
  static uint8_t li = 0;
  while (Vendor.available()) {
    char c = Vendor.read();
    if (c == '\n') {
      line[li] = '\0';
      if (li > 0) {
        Serial.printf("[hid] RX: %s\n", line);
        handleCommand(line);
      }
      li = 0;
    } else if (li < sizeof(line) - 1) {
      line[li++] = c;
    }
  }
}
