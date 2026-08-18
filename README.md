# m5-workspace

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

个人 M5Stack Arduino 项目集，全部基于 ESP32-S3（M5StickS3 / Cardputer），Windows + arduino-cli 开发。

## 项目

| 项目 | 是什么 | 状态 |
|---|---|---|
| [w96p-remote](w96p-remote/README.md) | 用 M5StickS3 通过 BLE 遥控 Witrn W96P/W66D 风扇：双键 + 体感手势 + 彩屏 UI | 协议层完成，遥控器待真机联调 |
| [liangzi-meter](liangzi-meter/README.md) | M5StickS3 峰谷电费桌面摆件：北京时间 + DeepSeek 峰谷状态 + 余额；PyQt6 上位机下发配置 | 可用 |
| [usb-poweroff](usb-poweroff/usb-poweroff.ino) | USB HID 关机最小固件 | 最小固件 |
| [espnow-smoke](espnow-smoke/espnow-smoke.ino) | ESP-NOW 冒烟 sketch（一次性验证产物） | 完成 |
| [kb](kb/README.md) | 硬件知识库（中文，官方来源）：设备规格/引脚、库 API、官方 demo 解析、ESP-NOW 专题 | 持续更新 |

## 目标设备

| 设备 | FQBN (m5stack:esp32 3.3.8) | 要点 |
|---|---|---|
| M5StickS3 (K150) | `m5stack:esp32:m5stack_sticks3` | 8MB OPI PSRAM、M5PM1 PMU、BMI270、IR 收发 |
| Cardputer-Adv (K132-Adv) | `m5stack:esp32:m5stack_cardputer` | 无 PSRAM、TCA8418 键盘、microSD、3.5mm 音频 |

## 编译

需要 [arduino-cli](https://arduino.github.io/arduino-cli/) 与 M5Stack 板卡包（`m5stack:esp32` 3.3.8）。

```bash
# 项目（项目级库 + 工作区共享库）
arduino-cli compile --libraries ./w96p-remote/lib --libraries ./lib --fqbn m5stack:esp32:m5stack_sticks3 w96p-remote/sticks3

# 独立 sketch
arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 liangzi-meter/sticks3
```

## 许可证

[MIT](LICENSE) © 2026 gggxbbb
