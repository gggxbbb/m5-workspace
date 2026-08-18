# m5-workspace

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

个人 M5Stack Arduino 开发工作区（Windows + arduino-cli）。本仓库是 project root：每个子目录是一个独立项目或共享资源，**开发由 agent 驱动**：需求 → 代码 → `arduino-cli compile` 验证 → 真机串口联调。

## 项目

| 路径 | 是什么 | 状态 |
|---|---|---|
| [`w96p-remote/`](w96p-remote/README.md) | Witrn W96P/W66D 风扇 BLE 遥控器：StickS3 双键 + IMU 手势 + 彩屏 UI；协议层为纯 C++ 库 `lib/w96p` | 协议层完成；遥控器待真机联调 |
| [`liangzi-meter/`](liangzi-meter/README.md) | StickS3 峰谷电费桌面摆件：北京时间 + DeepSeek 峰谷状态 + 余额；PyQt6 上位机经串口下发配置 | 可用 |
| [`usb-poweroff/`](usb-poweroff/usb-poweroff.ino) | USB HID 关机最小固件 | 最小固件 |
| [`espnow-smoke/`](espnow-smoke/espnow-smoke.ino) | ESP-NOW 冒烟 sketch（一次性验证产物，双板编译通过） | 完成 |
| [`kb/`](kb/README.md) | **硬件知识库**（中文，全部官方来源）——设备规格/引脚、库 API（本机源码核实）、官方 demo 收录、ESP-NOW 专题。**写代码前必读**，查证顺序见 `AGENTS.md` | 持续更新 |
| `AGENTS.md` | 仓库规则：铁律、编译命令、项目布局约定、agent skills 配置入口 | — |
| `docs/agents/` | agent 技能配置（issue tracker / triage 标签 / 领域文档规则），逐项目自治 | — |
| `lib/` | 工作区共享 Arduino 库（跨项目才放这；项目级库放 `<project>/lib/`） | — |

## 目标设备

| 设备 | FQBN (m5stack:esp32 3.3.8) | 要点 |
|---|---|---|
| M5StickS3 (K150) | `m5stack:esp32:m5stack_sticks3` | 8MB OPI PSRAM、M5PM1 PMU、BMI270、IR 收发 |
| Cardputer-Adv (K132-Adv) | `m5stack:esp32:m5stack_cardputer` | 无 PSRAM、TCA8418 键盘、microSD、3.5mm 音频 |

## 编译

```bash
arduino-cli compile --libraries ./<project>/lib --libraries ./lib --fqbn <FQBN> <project>/<branch>
```

`arduino-cli` 1.5.1 与 `pio` 6.1.19 在用户 PATH（本机以 arduino-cli 为准，不用 PIO）。验证 = 编译通过；无单测无 CI。细节与铁律见 `AGENTS.md`。

## 许可证

[MIT](LICENSE) © 2026 gggxbbb
