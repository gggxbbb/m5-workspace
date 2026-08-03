# M5Unified 库 KB（本地版本 0.2.19）

> **来源权威**：本文所有 API 与行为均核实自本机安装源码 `C:\Users\gameg\Documents\Arduino\libraries\M5Unified`（版本号见 `src/gitTagVersion.h`：0.2.19）。引用格式 `文件:行号` 均指该目录下路径。
>
> 适用目标设备：**M5StickS3**（FQBN `m5stack:esp32:m5stack_sticks3`）与 **Cardputer-Adv**（FQBN `m5stack:esp32:m5stack_cardputer`，无 Adv 专属板型）。设备级细节见 `kb/m5stick-s3.md`、`kb/cardputer-adv.md`。

## 1. 定位与适用设备

M5Unified 是 M5Stack 全系主机的**统一驱动库**：一个 `M5` 全局实例屏蔽各机型在显示、按键、电源管理（PMIC）、IMU、RTC、麦克风、扬声器上的差异，运行时**自动识别板型**并装配对应引脚与外设配置。应用代码只需 `#include <M5Unified.h>` 并 `M5.begin()`，同一固件逻辑可跨机型编译运行。

对 StickS3 / Cardputer-Adv 而言，M5Unified 负责：

- 显示屏初始化（经内嵌 M5GFX，ST7789 自动检测）
- 板型自动识别（见 §3）
- 内部 I2C 总线（`M5.In_I2C`）与外部 Port.A I2C（`M5.Ex_I2C`）装配
- 按键状态机（`M5.BtnA/B/C/BtnPWR`）
- 电源管理（StickS3 走 M5PM1；Cardputer/Cardputer-Adv 走 ADC 电量计）
- BMI270 IMU、ES8311 音频 codec（Mic/Speaker）的引脚与寄存器配置

**不负责**：Cardputer-Adv 的 56 键键盘（TCA8418，属 M5Cardputer 库）、红外收发、microSD 挂载（M5Unified 只提供引脚表 `M5.getPin()`/`M5.hasSD()`）。

## 2. 库间关系

```
应用代码
   │
   ▼
M5Unified ──依赖──▶ M5GFX（显示/board_t 枚举/板型检测的显示部分）
   │                     ▲
   ├── 被依赖 ──▶ M5Cardputer（键盘 TCA8418/74HC138、IR、SSH 示例依赖 LibSSH-ESP32）
   │
   └── 内嵌驱动 ──▶ src/utility/power/（AXP192/AXP2101/IP5306/M5PM1…）
                    src/utility/imu/（BMI270/MPU6886/SH200Q/BMM150…）
                    src/utility/rtc/（PCF8563/RX8130…）
```

- **M5GFX 是硬依赖**：`M5Unified.hpp` 直接 `#include <M5GFX.h>`；`board_t` 枚举实际定义在 M5GFX 的 `src/lgfx/boards.hpp`，M5Unified 只是 `using board_t = m5gfx::board_t;`（`src/M5Unified.hpp:24`）。
- **板型自动检测分两层**：M5GFX 在 `Display.init()` 时先通过 LCD SPI 总线与引脚strap识别板型（`M5GFX/src/M5GFX.cpp`），M5Unified 再用 `_check_boardtype()` 对无屏/歧义机型补充检测（`src/M5Unified.cpp:1352`）。
- **M5Cardputer 基于 M5Unified**：Cardputer-Adv 项目通常同时包含 `<M5Cardputer.h>` 与 `<M5Unified.h>`，键盘用 `M5Cardputer.Keyboard`，其余外设用 `M5.*`。
- **M5PM1 芯片驱动有两份**：M5Unified 内嵌 `src/utility/power/M5PM1_Class.hpp`（经 `M5.Power.M5pm1` 暴露）；独立的 M5PM1 库（github.com/m5stack/M5PM1）提供同名功能，**不要混用**（见 §5）。

## 3. 初始化模式（源码核实）

### 3.1 两种 begin

`src/M5Unified.hpp`（M5Unified 类内）：

```cpp
void begin(void) { config_t cfg; begin(cfg); }   // 全默认配置
void begin(config_t cfg) { ... }                  // 显式配置
```

- `M5.begin()` 等价于 `M5.begin(M5.config())`。需要改配置时用 `auto cfg = M5.config(); ...; M5.begin(cfg);`。
- **begin 只能执行一次**：`if (_board != board_t::board_unknown) { return; }`，重复调用静默返回。
- `begin(cfg)` 内部顺序：`Display.init()`（触发 M5GFX 板型检测）→ `_check_boardtype()` → `_setup_pinmap/_setup_i2c/_setup_led` → `_begin()`（按键引脚、PMIC、串口）→ `_begin_audio()`（Mic/Speaker 引脚与 codec 回调）→ `update()` → `_begin_rtc_imu()`。

### 3.2 config_t 关键字段（`src/M5Unified.hpp` config_t）

| 字段 | 默认 | 说明 |
|---|---|---|
| `serial_baudrate` | 0 | 非 0 时自动 `Serial.begin()`（仅 Arduino 环境） |
| `clear_display` | true | 启动时清屏 |
| `output_power` | true | 开机打开外部端口 5V 输出（对支持的机型） |
| `pmic_button` | true | 用 PMIC 电源键作为 `M5.BtnPWR`（StickS3 有效，见 §4.6） |
| `internal_imu` / `internal_rtc` / `internal_mic` / `internal_spk` | true | 启用内置 IMU/RTC/麦克风/扬声器 |
| `external_imu` / `external_rtc` | false | 探测 Port.A 上的 Unit Accel&Gyro / Unit RTC |
| `external_display_value` | 0xFFFF | 外接显示器自动探测总开关（各位对应 Module/Atom Display、Unit OLED/LCD/GLASS/RCA 等） |
| `external_speaker_value` | 0x00 | 外接扬声器（HAT SPK/ATOMIC SPK 等）默认全关 |
| `led_brightness` | 0 | 系统 LED 亮度 0–255（非 RGB LED） |
| `fallback_board` | 按芯片 | 自动检测失败时回退板型（ESP32-S3 默认 `board_M5AtomS3Lite`） |

### 3.3 StickS3 / Cardputer-Adv 的自动识别链路

1. `Display.init()` → M5GFX `autodetect`（`M5GFX/src/M5GFX.cpp`）：
   - **StickS3**：G47/G48 上拉检测通过后，在 I2C(0x6E) 读 M5PM1 device id，命中即 `board_M5StickS3`，并经 PM1_G2 打开 LCD 电源再检测 ST7789（`M5GFX.cpp:2575-2620`）。
   - **Cardputer / Cardputer-Adv**：先在 G33–G37 SPI 上读到 ST7789（id `0x81/0x85`），默认 `board_M5Cardputer`；再用 G5/G6/G8/G9 引脚strap区分——`(result & 0x0C) == 0x0C`（G8/G9 有上拉）即改判 `board_M5CardputerADV`（`M5GFX.cpp:2293-2350`）。VAMeter 也在此分支内区分。
2. `M5Unified::_check_boardtype(Display.getBoard())`（`M5Unified.cpp:1352`）：ESP32-S3 下先按 `get_pkg_ver()` 分支（QFN56/LGA56），对 M5GFX 已识别的板型直接透传；仅 `board_unknown` 时做补充探测，最终仍 unknown 则用 `cfg.fallback_board`。
3. **与 FQBN 的关系**：FQBN（`m5stack_sticks3` / `m5stack_cardputer`）决定编译期宏与 Flash/PSRAM 分区；**运行时板型完全靠自动检测**，不依赖 FQBN。用 `m5stack_cardputer` 编译的固件刷到 Cardputer-Adv 上会自动识别为 `board_M5CardputerADV`（这正是没有 Adv 专属 FQBN 也能正常工作的原因）。

### 3.4 检测后的板型枚举值（`M5GFX/src/lgfx/boards.hpp`）

```cpp
board_M5Cardputer    = 14
board_M5CardputerADV = 24
board_M5StickS3      = 26
```

运行时查询：`M5.getBoard()`（返回 `m5::board_t`，`M5Unified.hpp`）。板型字符串见 `M5GFX.cpp:3284/3301`（`getBoardName`）。

## 4. 核心 API 速查

> 全局实例 `extern m5::M5Unified M5;`（`src/M5Unified.hpp` 末尾）。成员对象：`Display / Lcd(别名) / Imu / Log / Power / Rtc / Touch / Speaker / Mic / Led / BtnA…BtnPWR / In_I2C / Ex_I2C`。

### 4.1 生命周期与杂项（`src/M5Unified.hpp`）

| API | 说明 |
|---|---|
| `M5.config()` | 返回 `config_t` 默认值，改完传给 `M5.begin(cfg)` |
| `M5.begin()` / `M5.begin(cfg)` | 初始化（仅首次有效） |
| `M5.update()` | **必须在 loop() 中每轮调用**；刷新按键、触摸、电源键状态 |
| `M5.getBoard()` | 当前板型 `board_t` |
| `M5.delay(ms)` / `M5.millis()` / `M5.micros()` | 跨平台时间函数 |
| `M5.getPin(pin_name_t)` | 查当前板型的引脚映射（如 `port_a_pin1`、`sd_spi_cs`、`rgb_led`） |
| `M5.hasSD()` / `M5.hasSDMMC()` | 是否有 SD 槽（Cardputer/Adv 为 true，StickS3 为 false） |
| `M5.getDisplayCount()` / `M5.Displays(i)` / `M5.setPrimaryDisplayType({...})` | 多显示器管理 |
| `M5.getUpdateMsec()` | 上次 `update()` 的毫秒时间戳 |

### 4.2 显示 `M5.Display`（M5GFX 实例，API 属 M5GFX）

- `M5.Display` 是 `M5GFX` 对象，`M5.Lcd` 是其引用别名（`M5Unified.hpp`）。
- 常用：`print/printf/println`、`setRotation`、`setBrightness(0–255)`、`width()/height()`、`fillRect/drawPixel/writeFastVLine`、`startWrite()/endWrite()`、`setEpdMode`（仅 EPD 机型）。完整绘制 API 见 M5GFX 库 KB。
- 两台目标机均为 ST7789：StickS3 135×240（G39/40/45/41/21/38），Cardputer/Adv 240×135（G33–G38，背光 G38）。

### 4.3 扬声器 `M5.Speaker`（`src/utility/Speaker_Class.hpp`）

| API | 说明 |
|---|---|
| `isEnabled()` | 是否配置了输出引脚（未启用内置 spk 时为 false） |
| `setVolume(0–255)` / `getVolume()` | 主音量；**默认 64**，不是 255 |
| `setChannelVolume(ch, 0–255)` | 8 个虚拟通道（0–7）各自音量 |
| `tone(freq, duration_ms=UINT32_MAX, channel=-1, stop_current=true)` | 方波提示音，后台任务播放 |
| `playWav(wav_data, len=~0u, repeat=1, channel=-1, stop=false)` | 播放含 WAV 头的数据 |
| `playRaw(data, len, sample_rate=44100, stereo=false, repeat=1, channel=-1, stop=false)` | 播放裸 PCM（int8/uint8/int16 三个重载；`playRAW` 已 deprecated） |
| `isPlaying()` / `isPlaying(ch)` / `getPlayingChannels()` | 播放状态查询 |
| `stop()` / `stop(ch)` | 停止 |
| `begin()` / `end()` | 开关 I2S 输出与 codec（`end()` 后经 `begin()` 可恢复） |
| `config()` / `config(cfg)` | `speaker_config_t` 读写（sample_rate 默认 48000） |

**板级默认配置**（`src/M5Unified.cpp` `_begin_audio`）：
- StickS3（`:2417`）：I2S_NUM_0，MCLK=G18/BCK=G17/WS=G15/DOUT=G14，**stereo=true，sample_rate=22050，magnification=1**；使能回调 `_speaker_enabled_cb_sticks3`（`:511`）经 M5PM1 PM1_G3 控制 AW8737 功放电源 + 写 ES8311 DAC 寄存器。
- Cardputer-Adv（`:2622`）：I2S_NUM_1，BCK=G41/WS=G43/DOUT=G42，magnification=16，单声道；使能回调 `_speaker_enabled_cb_cardputer_adv`（`:808`）写 ES8311 DAC 寄存器。初代 Cardputer 同引脚但**无** ES8311 回调（NS4168 直接驱动）。

### 4.4 麦克风 `M5.Mic`（`src/utility/Mic_Class.hpp`）

| API | 说明 |
|---|---|
| `isEnabled()` / `isRunning()` | 可用性/任务状态 |
| `begin()` / `end()` | 开关 I2S 输入与 codec |
| `record(buf, len, sample_rate, stereo=false)` / `record(buf, len)` | 录音到 int16/uint8 缓冲；返回值表示是否录满 |
| `isRecording()` | 0=未录 / 1=录制中(队列有空) / 2=录制中(队列满) |
| `setSampleRate(hz)` | 改采样率（`mic_config_t` 默认 16000） |
| `config()` / `config(cfg)` | `mic_config_t`（over_sampling、magnification、noise_filter_level 等） |

**板级默认配置**（`src/M5Unified.cpp`）：
- StickS3（`:2215`）：I2S_NUM_1，MCLK=G18/BCK=G17/WS=G15/DIN=G16，回调 `_microphone_enabled_cb_sticks3` 写 ES8311 ADC 寄存器。
- Cardputer-Adv（`:2291`）：DIN=G46/WS=G43/BCK=G41，回调 `_microphone_enabled_cb_cardputer_adv` 写 ES8311 ADC 寄存器（ADC volume ±0dB）。
- 初代 Cardputer（`:2283`）：DIN=G46/WS=G43，无 codec 回调（SPM1423 硅麦直连）。

### 4.5 IMU `M5.Imu`（`src/utility/IMU_Class.hpp`；BMI270 驱动在 `src/utility/imu/BMI270_Class.hpp`）

检测方式：`IMU_Class::begin` 在指定 I2C 总线上依次尝试 BMI270（地址 0x68，失败换 0x69）等芯片（`src/utility/IMU_Class.cpp:76-87`）。StickS3 与 Cardputer-Adv 的内部 I2C 上均有 BMI270 → `getType()==imu_bmi270`；初代 Cardputer 无 IMU → `imu_none`、`isEnabled()==false`。

| API | 说明 |
|---|---|
| `isEnabled()` / `getType()` | 是否可用 / `imu_t`（`imu_bmi270` 等） |
| `update()` | 读取最新原始数据，返回 `sensor_mask_t`（accel/gyro/mag 位） |
| `getImuData(&data)` / `getImuData()` | 一次取 9 轴：`imu_data_t{accel, gyro, mag}` |
| `getAccel(&x,&y,&z)` / `getGyro(...)` / `getMag(...)` | 单传感器取值（单位 g / dps / µT）；`getAccelData/getGyroData` 是**别名**，等价 |
| `getTemp(&t)` | 温度 |
| `setAxisOrder(...)` / `setAxisOrderRightHanded(a0,a1)` / `LeftHanded` | 自定义轴向 |
| `setCalibration(accel,gyro,mag)` | 自动校准强度 0–255，0=关 |
| `saveOffsetToNVS()` / `loadOffsetFromNVS()` / `clearOffsetData()` | 校准值持久化 |

BMI270 无磁力计：`getMag` 恒失败，`imu_data_t.mag` 无意义。

### 4.6 电源 `M5.Power`（`src/utility/Power_Class.hpp`，实现 `src/utility/Power_Class.cpp`）

通用 API：

| API | 说明 |
|---|---|
| `getBatteryLevel()` | 电量 0–100。StickS3：M5PM1 电压曲线（`Power_Class.cpp:1618`）；Cardputer/Adv：G10 ADC 分压（`_adc_ratio=2.0`，`:353-358`） |
| `isCharging()` | 返回 `is_charging_t{is_discharging=0, is_charging=1, charge_unknown=2}`。StickS3 经 PM1_G0 充电状态脚（`:1959`）；**Cardputer/Adv 恒返回 `charge_unknown`**（无充电检测硬件，`:1998` default 分支） |
| `getBatteryVoltage()` | 电池电压 mV。StickS3 → `M5pm1.getBatteryVoltage()`（`:1553`）；Cardputer/Adv → ADC |
| `setBatteryCharge(bool)` / `setChargeCurrent(mA)` / `setChargeVoltage(mV)` | 充电控制（M5PM1 机型有效） |
| `setExtOutput(bool)` / `getExtOutput()` | 外部端口 5V 输出。StickS3 → `M5pm1.setExtOutput`（`:737`）；Cardputer/Adv **无效**（无此电路） |
| `powerOff()` | 整机断电（StickS3 → `M5pm1.powerOff()`，`:1128`） |
| `timerSleep / deepSleep / lightSleep` | 休眠与定时唤醒 |
| `getKeyState()` | 电源键按压（0=无/1=长按/2=短按/3=both），读一次清零；M5PM1 → `getPekPress` |
| `getType()` | 当前 `pmic_t`：StickS3=`pmic_m5pm1`，Cardputer/Adv=`pmic_adc` |

**M5PM1 直通封装**（仅 ESP32-S3 编译目标存在，`Power_Class.hpp`）：`M5.Power.M5pm1`，类型 `M5PM1_Class`（`src/utility/power/M5PM1_Class.hpp`），含 `setExtOutput/getExtOutput`、`setLDOOutput`、`getPowerSource()`（`vin/vinout/battery/unknown`）、`getVBUSVoltage()`、`getBatteryVoltage()`、`get5VoutVoltage()`、`setChargeCurrent(8–512mA)`、`setChargeVoltage(3600–4545mV)`、`powerOff()`、5 路 GPIO 配置（`setGPIOFunction/Mode/Pull/Drive/Output/getGPIOInput`）与 IRQ/wake 管理。StickS3 红外收发供电、Grove 5V 方向控制都走这里。

### 4.7 按键 `M5.BtnA/B/C/BtnEXT/BtnPWR`（`src/utility/Button_Class.hpp`）

状态查询（全部**依赖 `M5.update()` 刷新**）：`isPressed/isReleased`、`wasPressed/wasReleased`、`wasClicked/wasHold/wasSingleClicked/wasDoubleClicked`、`pressedFor(ms)/releasedFor(ms)/wasReleaseFor(ms)`、`isHolding()`、`getClickCount()`；阈值 `setDebounceThresh/setHoldThresh`。

**物理映射**（`src/M5Unified.cpp` update 分支）：
- StickS3（`:3070`）：BtnA=KEY1(G11)，BtnB=KEY2(G12)，无 BtnC；BtnPWR=M5PM1 电源键（`pmic_button` 默认开，`Power_Class.cpp:1905` 对 `pmic_m5pm1` 启用）。
- Cardputer/Cardputer-Adv（`:3016`）：BtnA=G0（Boot 键），无 BtnB/C；无 BtnPWR。

### 4.8 RTC `M5.Rtc`（`src/utility/RTC_Class.hpp`）

`RTC_Class::begin` 的板型 switch（`src/utility/RTC_Class.cpp:44-64`）**不含** StickS3 / Cardputer / Cardputer-Adv → `_rtc_instance` 为空，`M5.Rtc.isEnabled()==false`，一切 `getDateTime/setDateTime` 静默无效。这三台机器需要用 RTC 时必须挂外置 Unit RTC 并设 `cfg.external_rtc=true`。API 备查：`getDateTime/setDateTime`、`setTimerIRQ(ms)`、`setAlarmIRQ`、`setSystemTimeFromRtc()`。

### 4.9 触摸 `M5.Touch`（`src/utility/Touch_Class.hpp`）

三台目标机均**无触摸面板** → `M5.Touch.isEnabled()==false`。API 仅供触屏机型（Core2/CoreS3 等）：`getCount()`、`getDetail(i)`（含 `state`：`touch/hold/flick/drag` 等 `touch_state_t`）、`getTouchPointRaw(i)`。对 StickS3/Cardputer-Adv 不要写触摸代码。

## 5. 易错点与反模式

1. **混用 M5Unified 与设备专属旧库**：StickS3 项目**禁止** `#include <M5StickC.h>/<M5StickCPlus2.h>`（AXP192 时代 API，硬件完全不同）；Cardputer-Adv 的键盘/红外用 `M5Cardputer` 库，电源/IMU/音频用 `M5Unified`，二者职责不要交叉。同一草图引入两套 begin 会导致 I2C/I2S 重复初始化。
2. **忘调 `M5.update()`**：按键所有 `was*/is*` 状态、`BtnPWR`、触摸都由它推进（`M5Unified.hpp` update 注释）。loop 里漏掉或长时间阻塞，按键表现像"失灵"。
3. **Speaker 与 Mic 互斥（同一 codec/总线）**：官方 Microphone 示例的标准写法是先 `M5.Speaker.end(); M5.Mic.begin();`（`examples/Basic/Microphone/Microphone.ino`），反之亦然。StickS3/Cardputer-Adv 的 ES8311 ADC/DAC 寄存器由各自 enable 回调切换，同时 begin 会互相覆盖。
4. **音量默认值假设错误**：`setVolume` 范围 0–255 但**默认只有 64**（`Speaker_Class.hpp` `_master_volume = 64`）；StickS3 电池供电时官方建议音量 <75%，防大电流重启。
5. **auto-detect ≠ FQBN**：板型是运行时检测的，FQBN 只影响编译配置。**不要**在代码里用 `#ifdef ARDUINO_M5STACK_CARDPUTER` 之类的宏推断 Adv/初代差异——同一个 FQBN 编出的固件两台机器都跑；正确做法是 `if (M5.getBoard() == m5::board_t::board_M5CardputerADV)` 运行时分支。
6. **PSRAM 差异**：StickS3（ESP32-S3-PICO-1-N8R8）有 8MB OPI PSRAM（PlatformIO 需 `board_build.arduino.memory_type = qio_opi` 与 `-DBOARD_HAS_PSRAM`）；Cardputer-Adv（ESP32-S3FN8）**无 PSRAM**。跨设备移植使用 `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` 或大音频缓冲的代码时，Adv 上会返回 NULL，必须判空或改用内部 RAM。
7. **假设 RTC 存在**：`M5.Rtc.isEnabled()` 在三台目标机上均为 false（§4.8），盲调 `getDateTime` 得到的是未初始化数据且返回 false。
8. **假设 `isCharging()` 返回充电/放电**：Cardputer/Adv 永远返回 `charge_unknown`，判断逻辑必须处理第三个枚举值，不要用 if/else 二分。
9. **假设有 BtnC/BtnPWR**：StickS3 只有 BtnA/BtnB/BtnPWR，Cardputer/Adv 只有 BtnA。`BtnC.wasClicked()` 编译能过（对象是固定数组引用）但永远无事件。
10. **deprecated API 别再用**：`Speaker.playRAW`（改 `playRaw`）、`Power.setExtPower`（改 `setExtOutput`）、`RTC8563_Class`（改 `RTC_Class`）、`board_M5ATOM/board_M5AtomEcho` 枚举别名，均带 `[[deprecated]]` 标记。
11. **文件系统/外接显示头文件顺序**：SD/SPIFFS/LittleFS 及 `M5UnitOLED` 等外接显示头必须**在** `<M5Unified.h>` **之前** include（`M5Unified.hpp` 顶部注释与 HowToUse 示例），否则对应 `cfg.external_display.*` 分支不编译。
12. **StickS3 红外接收与扬声器功放冲突**：用 IR 接收前需关功放（经 M5PM1），详见 `kb/m5stick-s3.md` 已知坑 §3；这不是 M5Unified 自动处理的。

## 6. 版本兼容

| 项 | 要求/事实 | 来源 |
|---|---|---|
| 本机安装版本 | 0.2.19 | `src/gitTagVersion.h` |
| StickS3 官方要求 | M5Unified ≥ 0.2.12，M5GFX ≥ 0.2.18 | docs.m5stack.com/en/core/StickS3 |
| Cardputer/Adv 官方要求 | M5Unified ≥ 0.2.8，M5GFX ≥ 0.2.10，M5Cardputer ≥ 1.1.0 | docs.m5stack.com/en/arduino/m5cardputer/program |
| `board_M5CardputerADV` 枚举值 | 24（M5GFX `src/lgfx/boards.hpp:35`） | 低版本 M5GFX 无此枚举，**升级 M5Unified 必须同步升级 M5GFX** |
| `board_M5StickS3` 枚举值 | 26（`boards.hpp:37`） | 同上 |
| Arduino core | m5stack:esp32 ≥ 3.2.2（Cardputer）/ ≥ 3.2.5（StickS3）；本机 3.3.8 满足 | 官方文档 |

功能支持矩阵（源码核实；三台机均无触摸与 RTC）：

| 功能 | StickS3 | Cardputer（初代/v1.1） | Cardputer-Adv |
|---|---|---|---|
| `board_t` | `board_M5StickS3`(26) | `board_M5Cardputer`(14) | `board_M5CardputerADV`(24) |
| Display (ST7789) | ✅ 135×240 | ✅ 240×135 | ✅ 240×135 |
| BtnA / BtnB / BtnC / BtnPWR | G11 / G12 / ✗ / PM1 PEK | G0 / ✗ / ✗ / ✗ | G0 / ✗ / ✗ / ✗ |
| Speaker | ✅ ES8311+AW8737，立体声 I2S0 | ✅ NS4168，I2S1，无 codec 回调 | ✅ ES8311+NS4150B，I2S1 |
| Mic | ✅ ES8311 ADC，I2S1 | ✅ SPM1423，无 codec 回调 | ✅ ES8311 ADC |
| IMU (BMI270) | ✅ 0x68/0x69 | ✗ `imu_none` | ✅ 0x68/0x69 |
| RTC | ✗ | ✗ | ✗ |
| Touch | ✗ | ✗ | ✗ |
| 电池电量 | ✅ M5PM1 电压曲线 | ✅ G10 ADC | ✅ G10 ADC |
| `isCharging()` | ✅ PM1_G0 | `charge_unknown` | `charge_unknown` |
| `setExtOutput` 5V | ✅ M5PM1 BOOST | ✗ | ✗ |
| `powerOff()` | ✅ M5PM1 断电 | ✗ 无 PMIC 与 power_hold 引脚，不能真正断电（`Power_Class.cpp:1108` default 分支） | 同左 |
| SD 槽 (`hasSD()`) | ✗ | ✅ SPI G40/14/39/12 | ✅ SPI G40/14/39/12 |
| RGB LED (`rgb_led` 引脚) | ✗（无表项） | G21 | G21（需先拉高 G38 使能） |
| PSRAM | 8MB OPI | 无（S3FN8） | 无（S3FN8） |

## 7. 最小可编译示例

与 `examples/Basic/HowToUse/HowToUse.ino`、`examples/Basic/Microphone/Microphone.ino`、`examples/Basic/Speaker/Speaker.ino` 用法核对一致。StickS3 / Cardputer-Adv 通用（运行时自动区分）：

```cpp
#include <M5Unified.h>   // 如需 SD/外接显示器，相关头文件必须在此行之前

void setup(void)
{
  auto cfg = M5.config();
  // cfg.serial_baudrate = 115200;  // 需要 Serial 时打开（默认 0 不初始化）
  // cfg.external_display_value = 0; // 不接外置显示器时建议关掉自动探测，加速启动
  M5.begin(cfg);

  M5.Display.setRotation(1);                 // 横屏（HowToUse 示例模式）
  M5.Display.setBrightness(128);
  M5.Display.println("M5Unified OK");

  if (M5.Speaker.isEnabled())
  {
    M5.Speaker.setVolume(64);                // 0~255，默认即 64
    M5.Speaker.tone(2000, 100);              // 2000Hz 100ms
    while (M5.Speaker.isPlaying()) { M5.delay(1); }
  }
}

void loop(void)
{
  M5.update();                               // 必须每轮调用

  if (M5.BtnA.wasClicked())
  {
    auto board = M5.getBoard();
    M5.Display.printf("board=%d bat=%d%% chg=%d\n",
      (int)board, M5.Power.getBatteryLevel(), (int)M5.Power.isCharging());
  }

  if (M5.Imu.isEnabled())                    // StickS3 / Cardputer-Adv 为 true
  {
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);          // 等价 getAccelData
  }
}
```

录音（互斥模式，源自 Microphone 示例）：

```cpp
M5.Speaker.end();   // 麦克风与扬声器不能同时用
M5.Mic.begin();
static int16_t rec[256];
if (M5.Mic.record(rec, 256, 16000)) { /* 录满一帧 */ }
```

StickS3 专属：Grove/IR 需要 5V 时在 `M5.begin` 后 `M5.Power.setExtOutput(true);`（见 `kb/m5stick-s3.md`）。

## 8. 参考链接

- 库源码（GitHub）：https://github.com/m5stack/M5Unified
- 依赖显示库：https://github.com/m5stack/M5GFX
- 板型 ID 注册表（board_t 编号来源）：https://github.com/m5stack/m5stack-board-id
- StickS3 官方文档：https://docs.m5stack.com/en/core/StickS3 · Arduino 上手：https://docs.m5stack.com/en/arduino/m5sticks3/program
- Cardputer/Adv Arduino 上手：https://docs.m5stack.com/en/arduino/m5cardputer/program
- M5PM1 教程（StickS3）：https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- 本仓库设备 KB：`kb/m5stick-s3.md`、`kb/cardputer-adv.md`
