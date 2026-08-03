# M5Stack 工作区

M5Stack Arduino 开发工作区（Windows + arduino-cli）。本目录是 project root，每个子目录是一个独立项目或共享资源。**开发由 agent 驱动**：需求 → 代码 → `arduino-cli compile` 验证 → 真机联调靠串口日志。

## 目录索引

| 路径 | 是什么 |
|---|---|
| `kb/` | **硬件知识库**（中文，全部官方来源）——设备规格/引脚、库 API（本机源码核实）、官方 demo 收录、ESP-NOW 专题。**写代码前必读**，查证顺序见 `AGENTS.md` |
| `AGENTS.md` | 仓库规则：铁律、编译命令、项目布局约定、agent skills 配置入口 |
| `docs/agents/` | agent 技能配置（issue tracker / triage 标签 / 领域文档规则），逐项目自治 |
| `lib/` | 工作区共享 Arduino 库（跨项目才放这；项目级库放 `<project>/lib/`） |
| `w96p-remote/` | **项目**：Witrn W96P/W66D 风扇 BLE 遥控器（见项目 README） |
| `espnow-smoke/` | ESP-NOW 冒烟 sketch（一次性验证产物，双板编译通过） |
| `M5Burner-v3-beta-win-x64/` | 官方刷机工具，**非项目文件**，已 gitignore |

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
