/*
 * usb-poweroff — 最小 USB HID 关机器
 *
 * 目标：StickS3 (m5stack_sticks3) / Cardputer-Adv (m5stack_cardputer)
 *       任意 ESP32-S3 + Arduino-ESP32 core 3.x（已验证 3.3.8）
 *
 * 行为：
 *   - 上电后等待 USB 枚举完成，自动发送 Windows 关机快捷键 Win+X → U → U
 *   - 之后按 BtnA（StickS3 = KEY1/G11）可手动再次触发
 *   - 上电时按住 BtnA 满 3 秒 → 取消本次自动触发（调试逃生舱）
 *
 * 注意：
 *   - 系统必须已登录且桌面可见，Win+X 菜单才可用（锁屏/登录界面无效）
 *   - 烧录完设备会自动复位，约 3 秒后电脑会关机，调试时按住 BtnA 上电可跳过
 *   - Cardputer 上 BtnA 无物理按键映射，仅自动触发生效
 */

#include <M5Unified.h>
#include <USB.h>
#include <USBHIDKeyboard.h>

USBHIDKeyboard kbd;

// 上电后多少秒自动触发一次关机（0 = 不自动，仅手动）
#define AUTO_TRIGGER_SECONDS 3
// 上电时按住 BtnA 满该时长（毫秒）→ 取消自动触发
#define CANCEL_HOLD_MS 3000

// 单键：按下 → 保持 → 释放
static void sendKey(uint8_t key, uint32_t hold_ms = 50) {
  kbd.press(key);
  delay(hold_ms);
  kbd.release(key);
  delay(30);
}

// 组合键：同时按下两个键 → 保持 → 全部释放
static void comboPress(uint8_t k1, uint8_t k2, uint32_t hold_ms = 80) {
  kbd.press(k1);
  kbd.press(k2);
  delay(hold_ms);
  kbd.releaseAll();
  delay(50);
}

// Windows 10/11 关机序列：Win+X → U → U
static void sendShutdown(void) {
  comboPress(KEY_LEFT_GUI, 'x');  // 打开快速链接菜单
  delay(500);                     // 等菜单弹出
  sendKey('u');                   // "关机或注销"（Shut down or sign out）
  delay(400);                     // 等子菜单展开
  sendKey('u');                   // "关机"（Shut down）
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  kbd.begin();  // 内部会启动 TinyUSB 并注册 HID 键盘接口

  if (AUTO_TRIGGER_SECONDS > 0) {
    // 逃生舱：上电即按住 BtnA，满 CANCEL_HOLD_MS 则跳过自动触发
    bool skipAuto = false;
    uint32_t t0 = millis();
    while (millis() - t0 < CANCEL_HOLD_MS) {
      M5.update();
      if (!M5.BtnA.isPressed()) break;
      delay(5);
    }
    skipAuto = (millis() - t0 >= CANCEL_HOLD_MS);

    if (!skipAuto) {
      delay(AUTO_TRIGGER_SECONDS * 1000);  // 等 USB 枚举 + 系统识别键盘
      sendShutdown();
    }
  }
}

void loop() {
  M5.update();  // 必须每轮调用，按钮状态依赖它
  if (M5.BtnA.wasClicked()) {
    sendShutdown();
  }
  delay(10);
}
