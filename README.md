# M5Stack Arduino 工作区

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

本仓库收录个人 M5Stack Arduino 项目与中文硬件知识库。各项目独立维护，主要面向 M5StickS3 和 Cardputer-Adv。开发环境为 Windows，编译工具为 Arduino CLI（`arduino-cli`）。

## 项目

| 项目 | 功能 | 状态 |
|---|---|---|
| [w96p-remote](w96p-remote/README.md) | 通过低功耗蓝牙（BLE）遥控 Witrn W96P/W66D 风扇，支持按键、体感手势和彩屏界面 | 协议层与 M5StickS3 固件已通过编译，待真机联调 |
| [liangzi-meter](liangzi-meter/README.md) | 显示北京时间、DeepSeek 峰谷状态与账户余额，通过 PyQt6 配置工具下发设置 | 可用 |
| [usb-poweroff](usb-poweroff/usb-poweroff.ino) | 通过 USB HID 发送关机快捷键 | 最小示例 |
| [espnow-smoke](espnow-smoke/espnow-smoke.ino) | 验证 ESP-NOW 无线通信 | 已通过编译 |

各项目的配置方法、验证记录与已知限制见项目文档。

## 目标设备

两款设备均采用 ESP32-S3，配备 8 MB 闪存。下表列出板卡包 `m5stack:esp32` 3.3.8 对应的完整板卡名称（FQBN）。

| 设备 | FQBN | 硬件特性 |
|---|---|---|
| M5StickS3（K150） | `m5stack:esp32:m5stack_sticks3` | 8 MB OPI PSRAM、M5PM1 电源管理芯片、BMI270 惯性传感器、红外收发 |
| Cardputer-Adv（K132-Adv） | `m5stack:esp32:m5stack_cardputer` | 无 PSRAM、TCA8418 键盘控制器、microSD 卡槽、3.5 mm 音频接口 |

## 编译

先安装 [Arduino CLI](https://arduino.github.io/arduino-cli/)、M5Stack 板卡包及项目所需的库。本工作区记录的工具链版本为 Arduino CLI 1.5.1 和 `m5stack:esp32` 3.3.8。库版本见[知识库索引](kb/README.md)。

在仓库根目录执行以下命令。

```bash
# 编译风扇遥控器，依次指定项目库和工作区共享库
arduino-cli compile --libraries ./w96p-remote/lib --libraries ./lib --fqbn m5stack:esp32:m5stack_sticks3 w96p-remote/sticks3

# 编译 DeepSeek 峰谷状态显示器
arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 liangzi-meter/sticks3
```

Cardputer-Adv 默认应用分区约为 1.25 MB。固件超出容量时，在编译命令中添加 `--board-options PartitionScheme=default_8MB`。M5StickS3 默认使用该分区方案。

编译通过仅表示完成构建检查。烧录步骤与真机验证要求见各项目文档。

## 开发文档

[中文知识库](kb/README.md)收录设备规格、引脚、库接口、官方示例与专题说明。开发前按以下顺序查阅：

1. 官方示例：[M5StickS3](kb/demos-sticks3.md)、[Cardputer-Adv](kb/demos-cardputer.md)。
2. 库接口：从[知识库索引](kb/README.md)查找对应的 `lib-*.md` 文档。
3. 硬件说明：[M5StickS3](kb/m5stick-s3.md)、[Cardputer-Adv](kb/cardputer-adv.md)。

项目专用库放在 `<project>/lib/`，跨项目共享库放在根目录 `lib/`。每个 Arduino 程序的目录名须与 `.ino` 文件名一致。完整目录约定见 [AGENTS.md](AGENTS.md)。

## 许可证

[MIT](LICENSE) © 2026 gggxbbb
