# M5StickS3(StickS3)

> **命名核实**:官方产品名为 **StickS3**(SKU: K150),商店页写作 "M5StickS3 ESP32-S3 Mini IoT Dev Kit"。它是真实存在的 ESP32-S3 Stick 系列新品(2025 年底发布),与 M5StickC / M5StickC Plus / M5StickC Plus2(ESP32-PICO 系列)是完全不同的设备,**不要**混用它们的板型与库。
>
> 主要来源:[docs.m5stack.com/en/core/StickS3](https://docs.m5stack.com/en/core/StickS3)、[shop.m5stack.com 产品页](https://shop.m5stack.com/products/m5sticks3-esp32s3-mini-iot-dev-kit)。

## 产品概述

StickS3 是 M5Stack Stick 系列最新的紧凑型可编程控制器,面向遥控、智能家居与 IoT/AI 语音场景。主控为 ESP32-S3-PICO-1-N8R8(8MB Flash + 8MB Octal PSRAM),配 1.14 寸 LCD、6 轴 IMU、ES8311 音频 codec + MEMS 麦克风 + 1W 扬声器、红外收发一体、250mAh 电池,顶部为 Hat2-Bus(2.54mm 16P),侧面 HY2.0-4P(Grove 规格)接口。背部磁吸设计。电源管理采用 M5Stack 自研的 **M5PM1** 芯片(I2C 0x6E),这是它与旧 StickC 系列(AXP192)最大的软件差异。

官方支持开发平台:Arduino、UiFlow2、ESP-IDF、PlatformIO。
([来源](https://docs.m5stack.com/en/core/StickS3))

## 核心规格

| 项目 | 参数 |
|---|---|
| SoC | ESP32-S3-PICO-1-N8R8,Xtensa 32-bit LX7 双核 @ 240MHz |
| Flash | 8MB |
| PSRAM | 8MB Octal(OPI) |
| 无线 | 2.4GHz Wi-Fi + BLE(ESP32-S3 内置) |
| 屏幕 | ST7789P3,135×240,1.14" |
| IMU | BMI270(6 轴,I2C 地址 0x68) |
| 音频 Codec | ES8311,24-bit,I2S(I2C 地址 0x18) |
| 麦克风 | MEMS 麦克风,SNR 65dB |
| 扬声器 | AW8737 功放 + 8Ω@1W 2011 腔体喇叭 |
| 红外 | IR 发射(G46)+ IR 接收(G42),接收必须用 RMT 外设 |
| PMU | M5PM1(I2C 地址 0x6E),电池充电/电量/电源轨管理 |
| 电池 | 250mAh 锂电 |
| RTC | **无独立 RTC 芯片**(官方规格表与原理图均未列出)[未确认 — 未见官方明确说明,但 PinMap 中无 RTC 器件] |
| 蜂鸣器 | **无独立蜂鸣器**;发声走 ES8311 + 扬声器 |
| 按键 | KEY1(G11)、KEY2(G12),侧面复位/电源键(单击开机/复位,双击关机,长按进下载模式) |
| 供电 | USB Type-C DC 5V;Grove/Hat EXT_5V 可配为 5V 输入或输出 |
| Grove 带载 | 空载 5V,最大 4.88V@0.38A |
| 功耗 | 关机 4.2V@14.02µA;满载 4.2V@519.02mA |
| 尺寸/重量 | 48.0×24.0×15.0mm,20.0g |
| 工作温度 | 0 ~ 40°C |

来源:[官方规格表](https://docs.m5stack.com/en/core/StickS3)、[原理图 PDF](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/K150_Stick_S3_PRJ_V0.6_20251111_2025_11_17_16_10_24.pdf)。

## 引脚与 GROVE 口定义

### LCD(ST7789P3,SPI)

| ESP32-S3 | G39 | G40 | G45 | G41 | G21 | G38 |
|---|---|---|---|---|---|---|
| ST7789P3 | MOSI | SCK | RS(DC) | CS | RST | BL |

### IMU 与 PMU(内部 I2C 总线)

| ESP32-S3 | G48 | G47 |
|---|---|---|
| BMI270(0x68) | SCL | SDA |
| M5PM1(0x6E) | SCL | SDA |

### 音频(ES8311,I2S + I2C 控制)

| ESP32-S3 | G18 | G14 | G17 | G15 | G16 | G48 | G47 |
|---|---|---|---|---|---|---|---|
| ES8311(0x18) | MCLK | DOUT | BCLK | LRCK | DIN | SCL | SDA |

### 按键 / 红外

| 功能 | GPIO |
|---|---|
| KEY1 | G11(输入) |
| KEY2 | G12(输入) |
| IR_TX | G46 |
| IR_RX | G42 |

### HY2.0-4P(PORT.A,Grove 规格)

| 线色 | Black | Red | Yellow | White |
|---|---|---|---|---|
| 信号 | GND | 5V | G9 | G10 |

### Hat2-Bus(顶部 2.54mm 16P)

| PIN | LEFT | RIGHT | PIN |
|---|---|---|---|
| GND | 1 | 2 | G5 |
| EXT_5V | 3 | 4 | G4 |
| Boot | 5 | 6 | G6 |
| G1 | 7 | 8 | G7 |
| G8 | 9 | 10 | G43 |
| BAT | 11 | 12 | G44 |
| 3V3_L2 | 13 | 14 | G2 |
| 5V_IN | 15 | 16 | G3 |

来源:[官方 PinMap](https://docs.m5stack.com/en/core/StickS3)。

## Arduino 开发要点

### FQBN(arduino-cli / m5stack:esp32 core)

```
m5stack:esp32:m5stack_sticks3
```

该 FQBN 已经本机 `arduino-cli board listall`(m5stack:esp32 core 3.3.8)确认存在,Arduino IDE 开发板选项名为 **M5StickS3**。官方要求 Board Manager 版本 **>= 3.2.5**(3.3.8 满足)。
([来源](https://docs.m5stack.com/en/arduino/m5sticks3/program))

板型包索引 URL(如未添加):

```
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
```

编译命令示例(Windows,跳过实际构建,仅作参考):

```bash
arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 .\MySketch
arduino-cli upload  --fqbn m5stack:esp32:m5stack_sticks3 -p COMx .\MySketch
```

进入下载模式:按住侧面复位键约 2 秒,直到内部绿色 LED 闪烁。

### 所需库

| 库 | 用途 | 官方要求版本 | GitHub |
|---|---|---|---|
| M5Unified | 统一驱动(按键/电源/IMU/麦克风/扬声器/屏幕初始化) | >= 0.2.12 | https://github.com/m5stack/M5Unified |
| M5GFX | 屏幕绘制(ST7789P3) | >= 0.2.18 | https://github.com/m5stack/M5GFX |
| M5PM1 | 电源管理芯片驱动(EXT_5V、功放开关等) | — | https://github.com/m5stack/M5PM1 |

典型代码骨架:

```cpp
#include <M5Unified.h>

void setup() {
  auto cfg = M5.config();
  cfg.external_display = false;
  M5.begin(cfg);
  M5.Power.setExtOutput(true);  // 需要 Grove/IR 供电时打开 EXT_5V 输出
  M5.Display.println("StickS3 OK");
}

void loop() {
  M5.update();  // 必须,按键/电源状态依赖它
}
```

常用 API:`M5.Power.getBatteryLevel()`、`M5.Power.isCharging()`、`M5.Imu.getImuData()`、`M5.Speaker.tone()`、`M5.Mic`(录音)。
([API 示例索引](https://docs.m5stack.com/en/arduino/m5sticks3/program))

### platformio.ini 示例(官方原文)

```ini
[env:m5stack-sticks3]
platform = espressif32@6.12.0
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.partitions = default_8MB.csv
board_build.arduino.memory_type = qio_opi
build_flags =
    -DESP32S3
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
    -DCORE_DEBUG_LEVEL=5
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    M5Unified=https://github.com/m5stack/M5Unified
    M5PM1=https://github.com/m5stack/M5PM1
```

([来源](https://docs.m5stack.com/en/core/StickS3))

## 已知坑与注意事项

1. **EXT_5V_EN 默认关闭**:M5Unified 默认初始化下 EXT_5V 为输入模式,Grove/Hat EXT_5V 无电,IR 收发也不工作。无外部 5V 供电时必须 `M5.Power.setExtOutput(true)`。
2. **5V 供电方向警告**:EXT_5V 配为输出模式时,只能经 USB 或 Hat 的 5V_IN 供电;此时再从 Grove/EXT_5V 反灌 5V 有短路烧毁风险。
3. **红外接收必须用 RMT 外设**,不支持 GPIO 软件解码;且使用 IR 接收时**必须关闭扬声器功放**(经 M5PM1,见官方教程),否则接收异常。收发端需正对、间距不小于 30cm。
4. **电池供电下扬声器音量建议 < 75%**,否则大功耗可能导致意外重启。
5. **结构不兼容** Hat Mini JoyC(U156)、Hat Mini EncoderC(U157)、Hat 18650C(U080)。
6. **早期批次**开机后可能有轻微异响,官方称不影响功能。
7. **勿拆外壳**:可能损坏天线 PFC 电路。
8. 与旧 StickC 系列(AXP192 PMU、无 PSRAM、ESP32-PICO)软硬件均不通用;库认准 M5Unified,不要用 M5StickC / M5StickCPlus2 库。
9. ESP32-S3 原生 USB:上传固件若卡在枚举,确认 `ARDUINO_USB_CDC_ON_BOOT` 设置或按复位键进下载模式。

来源:以上 1–7 均出自[官方文档 Note 章节](https://docs.m5stack.com/en/core/StickS3);8、9 为 [INFERENCE] 开发经验补充。

## 参考链接

- 官方文档(规格/PinMap/原理图):https://docs.m5stack.com/en/core/StickS3
- Arduino 快速上手:https://docs.m5stack.com/en/arduino/m5sticks3/program
- M5PM1 电源管理教程:https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- 商店产品页:https://shop.m5stack.com/products/m5sticks3-esp32s3-mini-iot-dev-kit
- 原理图 PDF:https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/K150_Stick_S3_PRJ_V0.6_20251111_2025_11_17_16_10_24.pdf
- M5PM1 Datasheet:https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf
- 结构文件:https://github.com/m5stack/M5_Hardware/tree/master/Products/K150_StickS3/Structures
- 库:https://github.com/m5stack/M5Unified · https://github.com/m5stack/M5GFX · https://github.com/m5stack/M5PM1
- 产品对比(Stick 系列):https://docs.m5stack.com/en/products_selector/m5stick_compare?select=K150
