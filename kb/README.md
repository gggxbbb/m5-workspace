# M5Stack 知识库

Arduino 开发知识库，事实来源均为 M5Stack 官方文档，未核实项标注 [未确认]。

## 设备

| 文档 | 设备 | FQBN (m5stack:esp32 3.3.8) | 官方库 |
|---|---|---|---|
| [m5stick-s3.md](m5stick-s3.md) | M5StickS3 (K150) | `m5stack:esp32:m5stack_sticks3` | M5Unified / M5GFX / M5PM1 |
| [cardputer-adv.md](cardputer-adv.md) | Cardputer-Adv (K132-Adv) | `m5stack:esp32:m5stack_cardputer`（与初代共用，无 Adv 专属条目） | M5Cardputer ≥1.1.0 / M5Unified |

## 官方 API/Demo 页汇总（docs.m5stack.com 全量收录）

| 文档 | 覆盖 | 页数 |
|---|---|---|
| [demos-sticks3.md](demos-sticks3.md) | docs.m5stack.com/en/arduino/m5sticks3/ 全站（program/m5pm1/battery/button/display/imu/ir_nec/mic/speaker/wakeup） | 10/10 |
| [demos-cardputer.md](demos-cardputer.md) | docs.m5stack.com/en/arduino/m5cardputer/ 全站（program/battery/button/display/imu/ir_nec/keyboard/mic/sdcard/speaker） | 10/10 |

## 专题

| 文档 | 主题 |
|---|---|
| [esp-now.md](esp-now.md) | StickS3 ↔ Cardputer-Adv 无线直连（已编译验证），含核心 3.x 回调签名坑 |
| [lib-usbhid.md](lib-usbhid.md) | core 3.3.8 自带 USB HID 键盘（USBHIDKeyboard，ESP32-S3 原生 USB），含 Windows 关机快捷键示例 |
| [lib-tinyusb-usb.md](lib-tinyusb-usb.md) | **TinyUSB 实战**（真机验证）：MSC 虚拟 U 盘（跨扇区 READ10 坑、Windows 缓存限制）、USBHIDVendor 双向通道（OUTPUT→feature 路由坑）、主机调试方法 |

## 官方库（本地安装版本为权威）

| 文档 | 库 | 本地版本 | 一句话定位 |
|---|---|---|---|
| [lib-m5unified.md](lib-m5unified.md) | M5Unified | 0.2.19 | 统一硬件抽象层，自动识别板型；入口 `M5.begin()` |
| [lib-m5gfx.md](lib-m5gfx.md) | M5GFX | 0.2.26 | LovyanGFX fork 显示库；用 M5Unified 时不要重复实例化 |
| [lib-m5pm1.md](lib-m5pm1.md) | M5PM1 | 1.0.7 | StickS3 的 PMU（I2C 0x6E），**不是 AXP192**；EXT_5V 默认关闭 |
| [lib-m5cardputer.md](lib-m5cardputer.md) | M5Cardputer | 1.1.1 | Cardputer 键盘/音频封装；用它的 begin 就不要再调 M5.begin() |

## vibe coding 铁律（从库 KB 提炼）

1. **begin 二选一**：`M5Cardputer.begin()` 内部已调 `M5.begin()`，禁止两个都调
2. **StickS3 的 PMU 是 M5PM1**（0x6E），旧 StickC 的 AXP192 代码一概不适用
3. **Grove/Hat 5V 输出默认关闭**：StickS3 需 `M5.Power.setExtOutput(true)` 或 M5PM1 `setBoostEnable(true)`
4. **Speaker 与 Mic 互斥**：切换必须 `end()` 一个再 `begin()` 另一个
5. **loop() 必须持续调 `M5.update()` / `M5Cardputer.update()`**，否则按键/键盘失效
6. **Cardputer-Adv 无 PSRAM**，大 buffer/sprite 要省内存；StickS3 有 8MB OPI PSRAM
7. **Speaker 默认音量是 64 不是 255**

## 本地工具链

- arduino-cli 1.5.1 + m5stack:esp32 核心 3.3.8（含全部 M5 板型 FQBN）
- pio (PlatformIO) 6.1.19
- 两者均已加入用户 PATH（需重开终端）
