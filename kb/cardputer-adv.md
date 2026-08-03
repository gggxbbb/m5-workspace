# M5Stack Cardputer-Adv (SKU: K132-Adv)

## 产品概述

Cardputer-Adv 是 2025 年推出的卡片式可编程计算机升级版，基于 Stamp-S3A 核心模组（ESP32-S3FN8），集成 1.14 英寸 LCD 与 56 键迷你键盘。相比初代 Cardputer / Cardputer v1.1，Adv 版补齐了音频输入输出（ES8311 codec + MEMS 麦克风 + 3.5mm 耳机口）、6 轴 IMU（BMI270）、键盘专用 IO 扩展芯片（TCA8418）、EXT 2.54-14P 扩展总线，并升级为单节 1750mAh 电池。机身背面带磁铁可吸附金属表面，兼容乐高孔位，并新增挂绳孔。官方支持 Arduino、UiFlow2、ESP-IDF、PlatformIO 四种开发方式。

发布相关：原理图版本日期为 2025-06-20，售价约 $29.90 [未确认：发布日期与当前在售状态]。

## 核心规格

| 项目 | 参数 |
|---|---|
| 核心模组 | Stamp-S3A |
| SoC | ESP32-S3FN8，Xtensa 32-bit LX7 双核，最高 240MHz |
| Flash | 8MB（SoC 内置，FN8 后缀即 8MB flash；无 PSRAM） |
| 屏幕 | ST7789V2 驱动，1.14"，240×135 px |
| 键盘 | 56 键（4×14 布局），按键压力 160gf |
| 音频 codec | ES8311 |
| 扬声器 | NS4150B 功放 + 8Ω@1W 扬声器 |
| 麦克风 | MEMS 麦克风，SNR 65dB |
| IMU | BMI270（6 轴） |
| IR | 1× 红外发射管 |
| 外存 | microSD 卡槽 |
| 扩展口 | HY2.0-4P Grove + EXT 2.54-14P |
| 电池 | 1750mAh 锂电池 |
| 工作电流 | DC4.2V@120.2mA（Wi-Fi 132.3mA，BLE 154.6mA，待机 0.23µA） |
| 尺寸/重量 | 84.0×54.0×19.6mm，81.0g |
| 工作温度 | 0~40°C |

## 引脚与 GROVE 口定义

### LCD（ST7789V2）
| Stamp-S3A | G38 | G33 | G34 | G35 | G36 | G37 |
|---|---|---|---|---|---|---|
| ST7789V2 | DISP_BL | RST | RS | DAT | SCK | CS |

注意：G38 同时兼作 RGB LED 的电源使能（PWR_EN）。Stamp-S3A 相比旧 Stamp-S3 为 RGB LED 增加了独立电子开关，控制 RGB LED 前必须先把 G38 拉高。

### 音频（ES8311）
| Stamp-S3A | G8 | G9 | G41 | G46 | G43 | G42 |
|---|---|---|---|---|---|---|
| ES8311 | I2C SDA | I2C SCL | I2S SCLK | I2S ASDOUT | I2S LRCK | I2S DSDIN |

### IMU（BMI270）
挂在与音频相同的 I2C 总线上：SDA=G8，SCL=G9。

### 键盘（TCA8418RTWR）
| Stamp-S3A | G8 | G9 | G11 |
|---|---|---|---|
| TCA8418 | SDA | SCL | INT |

键盘经由 I2C 键盘扫描芯片 TCA8418 读取（初代 Cardputer 为 74HC138 矩阵扫描），有中断引脚 G11，支持多键同按。

### IR 发射
G44 = IR TX。

### 电池电压
G10 = 电池 ADC 采样。

### microSD（SPI）
| Stamp-S3A | G12 | G14 | G40 | G39 |
|---|---|---|---|---|
| microSD | CS | MOSI | CLK | MISO |

**容量与格式**（官方文档只要求 FAT32，未标容量上限）：≤32GB SDHC 卡出厂即 FAT32，即插即用；>32GB SDXC 卡出厂为 exFAT，Arduino 内置 `SD` 库不支持 exFAT，需重新格式化为 FAT32（用 Rufus/guiformat，FAT32 卷上限 2TB）或改用 SdFat 库的 SdExFat 原生支持。

### Grove 口（HY2.0-4P）
黑=GND，红=5V，黄=G2，白=G1（PORT.CUSTOM）。

### EXT 2.54-14P 扩展总线
| 功能 | 引脚 | 左 | 右 | 引脚 | 功能 |
|---|---|---|---|---|---|
| RESET | G3 | 1 | 2 | 5VIN | |
| INT | G4 | 3 | 4 | GND | |
| BUSY | G6 | 5 | 6 | 5VOUT | |
| SCK | G40 | 7 | 8 | G8 | I2C_SDA |
| MOSI | G14 | 9 | 10 | G9 | I2C_SCL |
| MISO | G39 | 11 | 12 | G13 | UART_RX |
| CS | G5 | 13 | 14 | G15 | UART_TX |

注意 EXT 总线上的 SPI 引脚（G40/G14/G39）与 microSD 复用同一 SPI 总线，I2C（G8/G9）与音频 codec、IMU、键盘芯片共用。

## Adv 与初代 Cardputer 对比

| 对比项 | Cardputer-Adv | Cardputer v1.1 | Cardputer（初代） |
|---|---|---|---|
| 核心模组 | Stamp-S3A | Stamp-S3A | Stamp-S3 |
| RGB LED 供电 | 与屏幕背光共用电源，逻辑优化（需 G38 使能） | 同左 | 直接供电 |
| 天线 | 优化天线设计，接收更好 | 优化天线设计 | 标准天线 |
| 电池 | 单节 1750mAh | 120mAh + 1400mAh | 120mAh + 1400mAh |
| 音频方案 | ES8311 codec + NS4150B 功放 | NS4168 功放 + SPM1423 硅麦 | NS4168 + SPM1423 |
| 音频接口 | 3.5mm 耳机口（插入时扬声器功放自动禁用） | 无 | 无 |
| IMU | BMI270（6 轴） | 无 | 无 |
| 键盘扫描 | TCA8418RTWR（I2C 键盘芯片） | 74HC138 矩阵 | 74HC138 矩阵 |
| 扩展口 | HY2.0-4P Grove + EXT 2.54-14P | HY2.0-4P | HY2.0-4P |
| 挂绳孔 | 有 | 无 | 无 |

## Arduino 开发要点

### FQBN / 开发板选择

m5stack:esp32 核心（经本机 arduino-cli 3.3.8 实测核对，`board listall` 输出）中 Cardputer **只有一个 FQBN，无 Adv 专属条目**：

```
m5stack:esp32:m5stack_cardputer
```

Cardputer-Adv 同样选择 `M5Cardputer` 板型编译（官方 Arduino 快速上手页面明确说明同时适用于 Cardputer 与 Cardputer-Adv）。硬件差异（TCA8418 vs 74HC138）由 M5Cardputer 库自动适配。

编译要求（官方文档）：
- M5Stack board manager ≥ 3.2.2
- M5Cardputer 库 ≥ 1.1.0
- M5Unified ≥ 0.2.8
- M5GFX ≥ 0.2.10

### 所需库

- **M5Cardputer**（主驱动库，README 明确支持 M5Cardputer 与 M5Cardputer-ADV）：https://github.com/m5stack/M5Cardputer
  - 依赖：M5Unified、M5GFX、IRremote、LibSSH-ESP32（Arduino Library Manager 安装时按提示全部下载）
  - 库内 `src/utility/Keyboard/KeyboardReader/` 同时包含 `IOMatrix`（74HC138）与 `TCA8418` 两种键盘读取实现，兼容两代硬件
- **M5Unified**：https://github.com/m5stack/M5Unified
- **M5GFX**（显示）：https://github.com/m5stack/M5GFX
- 工厂固件（ESP-IDF）：https://github.com/m5stack/M5Cardputer-UserDemo/tree/CardputerADV

### 键盘输入 API

`M5Cardputer.Keyboard`（Keyboard_Class），主循环必须调用 `M5Cardputer.update()` 刷新键盘状态，并避免阻塞操作：

```cpp
M5Cardputer.begin(cfg, true);  // enableKeyboard = true
// loop 中：
M5Cardputer.update();
if (M5Cardputer.Keyboard.isChange()) {          // 键盘状态是否变化
  if (M5Cardputer.Keyboard.isPressed()) {       // 当前按下键数
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    // status.word: 字符列表; status.del: 退格; status.enter: 回车
  }
}
bool p = M5Cardputer.Keyboard.isKeyPressed('A');       // 单键查询
uint8_t code = M5Cardputer.Keyboard.getKey(coor);      // 按坐标(0-13, 0-3)取 ASCII
```

### platformio.ini 示例（来自官方文档）

```ini
[env:m5stack-cardputer]
platform = espressif32@6.7.0
board = esp32-s3-devkitc-1
framework = arduino
upload_speed = 1500000
build_flags =
    -DESP32S3
    -DCORE_DEBUG_LEVEL=5
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    M5Cardputer=https://github.com/m5stack/M5Cardputer
```

## 已知坑与注意事项

1. **进下载模式**：先把侧面电源开关拨到 OFF，按住 G0 按钮的同时接通电源（或插 USB-C），然后松开，即进入 download mode。
2. **充电必须开机**：给设备充电时电源开关要拨到 ON。
3. **RGB LED 电源开关**：G38 是 RGB LED 电源使能，与屏幕背光共用。点亮 RGB LED 前必须先把 G38 置高；这也是 Stamp-S3A 与旧 Stamp-S3 的行为差异，移植初代代码时容易踩坑。
4. **3.5mm 耳机插入时扬声器功放会被禁用**——代码播放音频无声时先检查耳机口是否插着东西。
5. **键盘必须在 loop 中持续 `M5Cardputer.update()`**，且要减少阻塞操作，否则按键变化会漏采。
6. **无独立 Adv FQBN**：arduino-cli / Arduino IDE 中统一用 `m5stack:esp32:m5stack_cardputer` 编译，不要去找不存在的 Adv 板型（本机核心 3.3.8 实测确认）。
7. **EXT 总线与 microSD 共用 SPI**（G40/G14/G39），同时使用 microSD 和 EXT 上的 SPI 外设时注意片选与总线冲突。
8. **I2C 总线（G8/G9）挂载了多个器件**：ES8311、BMI270、TCA8418 及 EXT 总线，扩展 I2C 外设注意地址冲突 [未确认：各器件具体 I2C 地址，需查原理图]。
9. PSRAM：官方规格表未列 PSRAM，ESP32-S3FN8 封装无 PSRAM（对比 N8R2/N8R8 等带 R 的型号），需要大 RAM 的场景注意。

## 参考链接

- 官方产品文档：https://docs.m5stack.com/en/core/Cardputer-Adv
- 官方商店页（规格表来源）：https://shop.m5stack.com/products/m5stack-cardputer-adv-version-esp32-s3 （SKU K132-ADV）
- Arduino 快速上手（Cardputer + Adv 通用）：https://docs.m5stack.com/en/arduino/m5cardputer/program
- 键盘 API：https://docs.m5stack.com/en/arduino/m5cardputer/keyboard
- 显示 API：https://docs.m5stack.com/en/arduino/m5cardputer/display
- microSD API：https://docs.m5stack.com/en/arduino/m5cardputer/sdcard
- M5Cardputer 库：https://github.com/m5stack/M5Cardputer
- M5Unified：https://github.com/m5stack/M5Unified
- M5GFX：https://github.com/m5stack/M5GFX
- 工厂固件（ESP-IDF）：https://github.com/m5stack/M5Cardputer-UserDemo/tree/CardputerADV
- 原理图 PDF：https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Sch_M5CardputerAdv_v1.0_2025_06_20_17_19_58.pdf
- 结构文件：https://github.com/m5stack/M5_Hardware/tree/master/Products/K132-Adv_Cardputer-Adv/Structures
- 产品选型对比表：https://docs.m5stack.com/en/products_selector/m5cardputer_compare?select=K132-Adv
