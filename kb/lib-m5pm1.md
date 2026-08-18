# M5PM1 — M5Stack 自研 PM1 电源管理芯片驱动库

> **核实版本**: `1.0.7`（library.properties），本地路径 `<libraries>\M5PM1`
> **芯片**: M5Stack 自研 PM1，多功能电源管理 IC
> **I2C 地址**: `0x6E`（默认，宏 `M5PM1_DEFAULT_ADDR`）
> **I2C 速度**: 默认 100kHz，支持切换至 400kHz
> **适用设备**: StickS3（板型 `board_M5StickS3`）、StampS3Bat、CoreMatrix、PaperDIY/Color、ChainCaptain、StopWatch 等使用 M5PM1 的 M5Stack 产品

---

## 1. 定位与适用设备

M5PM1 是 M5Stack 自主设计的 PM1 电源管理芯片的驱动库，提供电池充放电管理、多路电源轨控制（DCDC 5V、LDO 3.3V、BOOST 5V）、5 路 GPIO（含 PWM/ADC/NeoPixel 复用）、看门狗、定时器、RTC RAM、中断等完整功能。

**何时使用这个库而不是别的：**

| 场景 | 库 |
|---|---|
| StickS3 / StampS3Bat 等使用 PM1 芯片的设备 | **M5PM1**（本库） |
| CoreS3 / Core2 等使用 AXP2101 的设备 | AXP2101（M5Unified 内置） |
| 旧 StickC / StickC Plus2 等使用 AXP192 的设备 | AXP192（M5Unified 内置） |
| 日常电源操作（读电量、使能 Grove 5V） | `M5.Power.*`（M5Unified 封装，推荐） |
| 高级 PM1 功能（GPIO/PWM/ADC/NeoPixel/定时唤醒/RTC RAM） | 直接用本库 `M5PM1` |
| ESP-IDF 项目（不含 M5Unified） | 直接用本库 |

**核心规则：PM1 芯片 ≠ AXP192/AXP2101。API、寄存器、I2C 地址完全不同，绝不能混用代码。**

---

## 2. 库间关系

### 依赖
- **无强制外部库依赖**。`M5PM1.h` 自带 `M5PM1_i2c_compat.h`（I2C 兼容封装层），可直接操作 `TwoWire`（Arduino）或 I2C 总线（ESP-IDF）。
- 可选检测：若项目中存在 `M5Unified.h`，兼容层自动启用 `M5PM1_HAS_M5UNIFIED_I2C`，支持 `begin(&M5.In_I2C, addr, speed)` 借用 M5Unified 的 I2C 句柄。

### 被谁封装
- **M5Unified**（`v0.2.19`）内建 `M5PM1_Class`（`src/utility/power/M5PM1_Class.hpp`），通过 `Power_Class` 的 `M5pm1` 成员驱动。板型 `board_M5StickS3` 对应 `pmic_t::pmic_m5pm1`。
- **`PY32PMIC_Class`** 是 `M5PM1_Class` 的别名（已 deprecated），指向同一个类。

### API 重叠区：`M5PM1`（独立库） vs `M5PM1_Class`（M5Unified 内嵌） vs `M5.Power`

| 操作 | 独立库 `M5PM1` | M5Unified `M5PM1_Class` | 推荐方式 `M5.Power` |
|---|---|---|---|
| 初始化 | `pm1.begin(&Wire, 0x6E, sda, scl, 100000)` | 由 M5Unified 自动初始化 | `M5.begin(cfg)` 自动处理 |
| 读电池电压 (mV) | `pm1.readVbat(&mv)` | `M5pm1.getBatteryVoltage()` | `M5.Power.getBatteryVoltage()` |
| 读电量百分比 | **无直接 API**（需自行换算） | **无直接 API** | `M5.Power.getBatteryLevel()` |
| 充电状态 | `pm1.getPowerSource(&src)` 判 `PWR_SRC_5VIN` | `M5pm1.getPowerSource()` | `M5.Power.isCharging()` |
| EXT_5V 输出使能 | `pm1.setBoostEnable(true)` | `M5pm1.setExtOutput(true)` | `M5.Power.setExtOutput(true)` |
| GPIO 控制 | `pm1.pinMode/gpioSetFunc` 等全套 | `M5pm1.setGPIOFunction/Mode` 等 | **无封装**，直接用 `M5.Power.M5pm1.*` |
| PWM/ADC/NeoPixel | 完整 API | **不封装** | **不封装**，必须用独立 `M5PM1` |
| 关机 | `pm1.shutdown()` | `M5pm1.powerOff()` | `M5.Power.powerOff()` |
| 定时唤醒 | `pm1.timerSet()` | **不封装** | **不封装**，必须用独立 `M5PM1` |
| RTC RAM (32B) | `pm1.writeRtcRAM/readRtcRAM` | **不封装** | **不封装**，必须用独立 `M5PM1` |
| 按钮行为禁用 | `pm1.setDoubleOffDisable/setSingleResetDisable` | **不封装** | **不封装**，必须用独立 `M5PM1` |

**关键事实：M5Unified 的 `M5PM1_Class` 只封装了基础电源操作 + GPIO 控制。PWM、ADC、NeoPixel、定时唤醒、RTC RAM、按钮行为配置等高级功能仅在独立 `M5PM1` 库中可用。**

> 代码来源：`M5PM1_Class.hpp` 只暴露了 `setExtOutput`、`setLDOOutput`、`setBatteryCharge`、GPIO 操作、电源读取和关机。`M5PM1_Class::isCharging()` 返回常量 `false`（实际充电检测在 `Power_Class::isCharging()` 里通过读 GPIO0 电平实现）。

---

## 3. 初始化模式

### 3.1 Arduino 独立使用（最常用）

```cpp
#include <M5PM1.h>

M5PM1 pm1;

// 签名（来自 M5PM1.h:1035）
m5pm1_err_t begin(
    TwoWire* wire = &Wire,          // I2C 总线指针
    uint8_t addr = M5PM1_DEFAULT_ADDR,  // 0x6E
    int8_t sda = -1,                // SDA 引脚（-1 用默认）
    int8_t scl = -1,                // SCL 引脚（-1 用默认）
    uint32_t speed = M5PM1_I2C_FREQ_100K  // 100000 Hz
);

// StickS3 典型调用（SDA=G47, SCL=G48）
pm1.begin(&Wire, M5PM1_DEFAULT_ADDR, 47, 48, M5PM1_I2C_FREQ_100K);
```

### 3.2 Arduino + M5Unified（复用 I2C 句柄）

```cpp
#include <M5Unified.h>
#include <M5PM1.h>

M5PM1 pm1;

// 签名（来自 M5PM1.h:1051，仅当 M5Unified 头文件存在时可用）
m5pm1_err_t begin(
    m5::I2C_Class* i2c,             // M5.In_I2C 指针
    uint8_t addr = M5PM1_DEFAULT_ADDR,
    uint32_t speed = M5PM1_I2C_FREQ_DEFAULT  // 100000
);

// 典型调用
pm1.begin(&M5.In_I2c, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K);
```

**注意：此重载仅在 `M5Unified.h` 先于 `M5PM1.h` 包含时可用**（`M5PM1_i2c_compat.h` 通过 `__has_include(<utility/I2C_Class.hpp>)` 检测）。M5PM1 只借用句柄，I2C 生命周期仍由 M5Unified 管理。

### 3.3 M5Unified 自动初始化（推荐日常使用）

```cpp
#include <M5Unified.h>

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);  // 自动初始化 PM1，设置板型特定 GPIO
    // 此后 M5.Power.* 可用
}
```

StickS3 的初始化代码（来自 `Power_Class.cpp:228-230`）：
```cpp
case board_t::board_M5StickS3:
    _pmic = pmic_t::pmic_m5pm1;
    M5pm1.setGPIOFunction(M5PM1_Class::gpio0, M5PM1_Class::gpio);
    M5pm1.setGPIOMode(M5PM1_Class::gpio0, M5PM1_Class::input);
    break;
```

---

## 4. 核心 API 速查表

> **所有签名均从 `M5PM1.h` 源码核实。返回类型 `m5pm1_err_t` 为 `int` 枚举：`M5PM1_OK = 0`，失败返回负值。**

### 4.1 初始化与设备信息

```cpp
// 初始化（见第3节）
m5pm1_err_t begin(TwoWire* wire = &Wire, uint8_t addr = 0x6E, int8_t sda = -1, int8_t scl = -1, uint32_t speed = 100000);
m5pm1_err_t begin(m5::I2C_Class* i2c, uint8_t addr = 0x6E, uint32_t speed = 100000);

// 设备信息
m5pm1_err_t getDeviceId(uint8_t* id);        // 通常返回 0x6E
m5pm1_err_t getDeviceModel(uint8_t* model);
m5pm1_err_t getHwVersion(uint8_t* version);
m5pm1_err_t getSwVersion(uint8_t* version);
```

### 4.2 电压读取（单位：mV）

```cpp
m5pm1_err_t readVbat(uint16_t* mv);       // 电池电压 (mV)
m5pm1_err_t readVin(uint16_t* mv);        // USB/VIN 输入电压 (mV)
m5pm1_err_t read5VInOut(uint16_t* mv);    // 5V IN/OUT 电压 (mV)
m5pm1_err_t readVref(uint16_t* mv);       // 参考电压 (mV)
m5pm1_err_t getRefVoltage(uint16_t* mv);  // readVref 别名
```

### 4.3 电源轨控制

```cpp
// 来自 M5PM1.h:1873-1920
m5pm1_err_t setBoostEnable(bool enable);   // BOOST 5V / Grove EXT_5V 输出使能 ★
m5pm1_err_t setDcdcEnable(bool enable);    // DCDC 5V 使能（影响芯片供电，慎关）
m5pm1_err_t setLdoEnable(bool enable);     // LDO 3.3V 使能（影响芯片供电，慎关）
m5pm1_err_t setChargeEnable(bool enable);  // 充电使能
m5pm1_err_t setLedEnLevel(bool level);     // LED_EN 默认电平（true=高）
m5pm1_err_t setPowerConfig(uint8_t mask, uint8_t value);  // 直接写 PWR_CFG 寄存器
m5pm1_err_t getPowerConfig(uint8_t* config);
m5pm1_err_t clearPowerConfig(uint8_t mask);

// 电源位掩码（m5pm1_pwr_cfg_t），可组合
M5PM1_PWR_CFG_CHG_EN    // (1<<0) 充电使能
M5PM1_PWR_CFG_DCDC_EN   // (1<<1) DCDC 5V 使能
M5PM1_PWR_CFG_LDO_EN    // (1<<2) LDO 3.3V 使能
M5PM1_PWR_CFG_BOOST_EN  // (1<<3) BOOST/Grove 5V 输出使能
M5PM1_PWR_CFG_LED_CTRL  // (1<<4) LED_EN 默认电平
```

### 4.4 电源状态

```cpp
m5pm1_err_t getPowerSource(m5pm1_pwr_src_t* src);
// 返回值：M5PM1_PWR_SRC_5VIN(=0) / _5VINOUT(=1) / _BAT(=2) / _UNKNOWN(=3)

m5pm1_err_t getWakeSource(uint8_t* src, m5pm1_clean_type_t cleanType = M5PM1_CLEAN_NONE);
m5pm1_err_t clearWakeSource(uint8_t mask);

// 唤醒源位掩码（m5pm1_wake_src_t）
M5PM1_WAKE_SRC_TIM       // 0x01 定时器唤醒
M5PM1_WAKE_SRC_VIN       // 0x02 VIN 插入唤醒
M5PM1_WAKE_SRC_PWRBTN    // 0x04 电源按钮唤醒
M5PM1_WAKE_SRC_RSTBTN    // 0x08 复位按钮唤醒
M5PM1_WAKE_SRC_CMD_RST   // 0x10 命令复位唤醒
M5PM1_WAKE_SRC_EXT_WAKE  // 0x20 外部 GPIO 唤醒
M5PM1_WAKE_SRC_5VINOUT   // 0x40 5VINOUT 插入唤醒
```

### 4.5 系统命令

```cpp
m5pm1_err_t shutdown();            // 关机
m5pm1_err_t reboot();              // 复位
m5pm1_err_t enterDownloadMode();   // 进入下载模式
m5pm1_err_t sysCmd(m5pm1_sys_cmd_t cmd);  // 通用命令接口
```

### 4.6 电池低压保护

```cpp
m5pm1_err_t setBatteryLvp(uint16_t mv);  // 阈值 mV，公式：mV = 2000 + reg * 7.81，范围 2000~4000mV
```

### 4.7 看门狗

```cpp
m5pm1_err_t wdtSet(uint8_t timeout_sec);  // 0=禁用, 1-255=秒
m5pm1_err_t wdtFeed();                    // 写入 0xA5 喂狗
m5pm1_err_t wdtGetCount(uint8_t* count);
```

### 4.8 定时器（含定时唤醒）

```cpp
m5pm1_err_t timerSet(uint32_t seconds, m5pm1_tim_action_t action);
m5pm1_err_t timerClear();

// 超时动作
M5PM1_TIM_ACTION_STOP     = 0b000  // 停止，无动作
M5PM1_TIM_ACTION_FLAG     = 0b001  // 仅设置标志
M5PM1_TIM_ACTION_REBOOT   = 0b010  // 系统复位
M5PM1_TIM_ACTION_POWERON  = 0b011  // 开机（定时唤醒用这个！）
M5PM1_TIM_ACTION_POWEROFF = 0b100  // 关机
```

**典型定时唤醒流程**（来自 `usb_interrupt_sleep.ino`）：
```cpp
pm1.timerSet(10, M5PM1_TIM_ACTION_POWERON);  // 10 秒后自动开机
pm1.shutdown();                                // 现在关机
```

### 4.9 按钮（PM1 硬件按钮交互）

```cpp
// 按钮状态（来自 M5PM1.h:2095-2130）
m5pm1_err_t btnGetState(bool* pressed);   // 当前按钮是否按下（实时）
m5pm1_err_t btnGetFlag(bool* wasPressed); // 按钮曾被按下标志（读后自动清除）
m5pm1_err_t btnSetConfig(m5pm1_btn_type_t type, m5pm1_btn_delay_t delay);

// 禁用硬件行为（高危！）
m5pm1_err_t setSingleResetDisable(bool disable);  // 禁用单击复位
m5pm1_err_t setDoubleOffDisable(bool disable);    // 禁用双击关机
m5pm1_err_t getSingleResetDisable(bool* disabled);
m5pm1_err_t getDoubleOffDisable(bool* disabled);

// 下载模式锁（高危！）
m5pm1_err_t setDownloadLock(bool lock);
m5pm1_err_t getDownloadLock(bool* lock);
```

**按钮延时配置**（影响双击/长按识别时间窗口）：
- 单击延时：`M5PM1_BTN_CLICK_DELAY_125MS` / `250MS` / `500MS` / `1000MS`
- 双击间隔：`M5PM1_BTN_DOUBLE_CLICK_DELAY_125MS` / `250MS` / `500MS` / `1000MS`
- 长按延时：`M5PM1_BTN_LONG_PRESS_DELAY_1000MS` / `2000MS` / `3000MS` / `4000MS`

### 4.10 中断

```cpp
// GPT 状态（读取 + 可选清除）
m5pm1_err_t irqGetGpioStatus(uint8_t* status, m5pm1_clean_type_t cleanType = M5PM1_CLEAN_NONE);
m5pm1_err_t irqGetSysStatus(uint8_t* status, m5pm1_clean_type_t cleanType = M5PM1_CLEAN_NONE);
m5pm1_err_t irqGetBtnStatus(uint8_t* status, m5pm1_clean_type_t cleanType = M5PM1_CLEAN_NONE);
m5pm1_err_t irqGetGpioStatusEnum(m5pm1_irq_gpio_t* gpio_num, m5pm1_clean_type_t cleanType);
m5pm1_err_t irqGetSysStatusEnum(m5pm1_irq_sys_t* sys_irq, m5pm1_clean_type_t cleanType);
m5pm1_err_t irqGetBtnStatusEnum(m5pm1_irq_btn_t* btn_irq, m5pm1_clean_type_t cleanType);

// 清除
m5pm1_err_t irqClearGpioAll();
m5pm1_err_t irqClearSysAll();
m5pm1_err_t irqClearBtnAll();

// 中断屏蔽
m5pm1_err_t irqSetGpioMask(m5pm1_irq_gpio_t pin, m5pm1_irq_mask_ctrl_t mask);
m5pm1_err_t irqSetSysMask(m5pm1_irq_sys_t event, m5pm1_irq_mask_ctrl_t mask);
m5pm1_err_t irqSetBtnMask(m5pm1_irq_btn_t type, m5pm1_irq_mask_ctrl_t mask);
m5pm1_err_t irqSetGpioMaskAll(m5pm1_irq_mask_ctrl_t mask);
m5pm1_err_t irqSetSysMaskAll(m5pm1_irq_mask_ctrl_t mask);
m5pm1_err_t irqSetBtnMaskAll(m5pm1_irq_mask_ctrl_t mask);
```

### 4.11 GPIO（Arduino 风格 + 高级 API）

```cpp
// Arduino 兼容
void pinMode(uint8_t pin, uint8_t mode);      // INPUT/OUTPUT/INPUT_PULLUP 等
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);

// 高级 GPIO
m5pm1_err_t gpioSet(m5pm1_gpio_num_t pin, m5pm1_gpio_mode_t mode, uint8_t value, m5pm1_gpio_pull_t pull, m5pm1_gpio_drive_t drive);
m5pm1_err_t gpioSetFunc(m5pm1_gpio_num_t pin, m5pm1_gpio_func_t func);       // GPIO/IRQ/WAKE/OTHER
m5pm1_err_t gpioSetMode(m5pm1_gpio_num_t pin, m5pm1_gpio_mode_t mode);       // INPUT/OUTPUT
m5pm1_err_t gpioSetOutput(m5pm1_gpio_num_t pin, uint8_t value);
m5pm1_err_t gpioGetInput(m5pm1_gpio_num_t pin, uint8_t* value);
m5pm1_err_t gpioSetPull(m5pm1_gpio_num_t pin, m5pm1_gpio_pull_t pull);
m5pm1_err_t gpioSetDrive(m5pm1_gpio_num_t pin, m5pm1_gpio_drive_t drive);
m5pm1_err_t gpioSetWakeEnable(m5pm1_gpio_num_t pin, bool enable);
m5pm1_err_t gpioSetWakeEdge(m5pm1_gpio_num_t pin, m5pm1_gpio_wake_edge_t edge);
```

**GPIO 特殊功能映射**（`FUNC_OTHER` = `0b11`）：

| GPIO | 特殊功能 | 备注 |
|---|---|---|
| GPIO0 | LED_EN (NeoPixel) | NeoPixel 仅此引脚支持，最大 32 LED |
| GPIO1 | ADC1 | 模拟输入 |
| GPIO2 | ADC2 | 模拟输入 |
| GPIO3 | PWM0 | 脉宽调制输出 |
| GPIO4 | PWM1 | 脉宽调制输出 |

### 4.12 ADC

```cpp
m5pm1_err_t analogRead(m5pm1_adc_channel_t channel, uint16_t* value);  // 0-4095 (12-bit)
m5pm1_err_t readTemperature(uint16_t* temperature);                     // 0.1°C 单位
m5pm1_err_t isAdcBusy(bool* busy);
m5pm1_err_t disableAdc();

// 通道枚举
M5PM1_ADC_CH_1    = 1   // ADC1 (GPIO1)，需先设 FUNC_OTHER
M5PM1_ADC_CH_2    = 2   // ADC2 (GPIO2)，需先设 FUNC_OTHER
M5PM1_ADC_CH_TEMP = 6   // 芯片内部温度传感器
```

### 4.13 PWM

```cpp
m5pm1_err_t setPwmFrequency(uint16_t frequency);  // Hz，全通道共享！
m5pm1_err_t setPwmDuty(m5pm1_pwm_channel_t channel, uint8_t duty, bool polarity = false, bool enable = true);  // 0-100%
m5pm1_err_t setPwmDuty12bit(m5pm1_pwm_channel_t channel, uint16_t duty12, bool polarity = false, bool enable = true);  // 0-4095
m5pm1_err_t setPwmConfig(m5pm1_pwm_channel_t channel, bool enable, bool polarity, uint16_t frequency, uint16_t duty12);
m5pm1_err_t analogWrite(m5pm1_pwm_channel_t channel, uint8_t value);  // Arduino 兼容 0-255

// 通道枚举
M5PM1_PWM_CH_0 = 0  // GPIO3
M5PM1_PWM_CH_1 = 1  // GPIO4
```

### 4.14 NeoPixel

```cpp
m5pm1_err_t setLedCount(uint8_t count);           // 1-32
m5pm1_err_t setLedColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
m5pm1_err_t setLedColor(uint8_t index, m5pm1_rgb_t color);
m5pm1_err_t refreshLeds();                        // 必须调用！否则不更新
m5pm1_err_t disableLeds();
m5pm1_err_t setLeds(const m5pm1_rgb_t* colors, uint8_t arraySize, uint8_t count, bool autoRefresh = true);
```

**NeoPixel 初始化流程**（来自 `neopixel.ino`）：
```cpp
pm1.pinMode(M5PM1_GPIO_NUM_0, M5PM1_OTHER);  // GPIO0 → 特殊功能
pm1.setLedEnLevel(true);                       // 电平高使能
pm1.setLedCount(1);                            // 灯珠数
// 然后 setLedColor + refreshLeds
```

### 4.15 RTC RAM

```cpp
m5pm1_err_t writeRtcRAM(uint8_t offset, const uint8_t* data, uint8_t len);  // offset: 0-31, len ≤ 32-offset
m5pm1_err_t readRtcRAM(uint8_t offset, uint8_t* data, uint8_t len);
// RTC RAM 共 32 字节，掉电/睡眠期间数据保持
```

### 4.16 AW8737A 音频功放脉冲控制（扬声器增益）

```cpp
m5pm1_err_t setAw8737aPulse(m5pm1_gpio_num_t pin, m5pm1_aw8737a_pulse_t num, m5pm1_aw8737a_refresh_t refresh = M5PM1_AW8737A_REFRESH_NOW);
m5pm1_err_t setAw8737aMode(m5pm1_gpio_num_t pin, m5pm1_aw8737a_mode_t mode, m5pm1_aw8737a_refresh_t refresh = M5PM1_AW8737A_REFRESH_NOW);
m5pm1_err_t refreshAw8737aPulse();
m5pm1_err_t refreshAw8737aMode();

// 增益模式
M5PM1_AW8737A_MODE_1 = 0  // 0 脉冲 (静音)
M5PM1_AW8737A_MODE_2 = 1  // 1 脉冲 (低增益)
M5PM1_AW8737A_MODE_3 = 2  // 2 脉冲 (中增益)
M5PM1_AW8737A_MODE_4 = 3  // 3 脉冲 (高增益)
```

---

## 5. 易错点与反模式

### 5.1 误用 AXP192/AXP2101 代码（最高频）

M5PM1 与 AXP192/AXP2101 是**完全不同**的 PMU 芯片。常见 vibe coding 错误：

```cpp
// ❌ 错误：AXP192 代码照搬到 StickS3
M5.Axp.begin();                    // StickS3 没有 AXP！
#define M5STICKC_PLUS2_AXP192_ADDR  // 这是旧 StickC 的
M5.Power.setPowerBoostOnOff(true); // AXP192 API，PM1 没有

// ✅ 正确
M5.begin(cfg);                     // M5Unified 自动识别 PM1
M5.Power.setExtOutput(true);       // PM1 EXT_5V 使能
```

**判断设备用哪个 PMU：参考 `kb/m5stick-s3.md` — StickS3 用 M5PM1 (0x6E)，StickC Plus2 用 AXP192 (0x34)。**

### 5.2 I2C 地址

- PM1 的 I2C 地址是 **0x6E**（`M5PM1_DEFAULT_ADDR`），不是 0x34（AXP192）或 0x57/0x68。
- StickS3 I2C 引脚：SDA=G47, SCL=G48（与 BMI270 IMU 共享总线）。

### 5.3 M5Unified 封装边界

- `M5.Power.getBatteryLevel()` **可用**：电压→百分比换算（3300mV=0%, 4150mV=100%），含错误处理。
- `M5.Power.isCharging()` **可用**：通过读 M5PM1 GPIO0 电平判断（StickS3：GPIO0 低电平=充电中）。
- `M5.Power.setExtOutput(true)` **可用**：操作 PM1 BOOST_EN 位。
- `M5.Power.M5pm1.*` 绕过 `Power_Class` 直接操作 `M5PM1_Class` — 仅在需要 GPIO 配置时。
- **PWM/ADC/NeoPixel/定时唤醒/RTC RAM/按钮行为配置** → 必须用独立库 `M5PM1`（`#include <M5PM1.h>`），`M5PM1_Class` 不封装这些功能。

### 5.4 EXT_5V (BOOST) 默认关闭

`BOOST_EN`（bit 3 of `PWR_CFG`）**默认关闭**。Grove / Hat EXT_5V 无输出，IR 收发不工作。必须显式使能：

```cpp
pm1.setBoostEnable(true);          // 独立库
// 或
M5.Power.setExtOutput(true);      // M5Unified
```

**警告**：EXT_5V 输出模式下，若从 Grove/EXT_5V 反灌 5V，有短路烧毁风险。

### 5.5 电源轨操作风险

- `setDcdcEnable(false)` / `setLdoEnable(false)` 可导致**芯片掉电**，仅在确认安全时操作。
- `setChargeEnable(false)` 停止电池充电。

### 5.6 按钮行为是 PM1 硬件控制的

- 双击关机、单击复位是 PM1 **硬件行为**（由寄存器 `BTN_CFG_1`/`BTN_CFG_2` 控制）。
- ESP32 的 `M5.update()` 读取的是 ESP32 GPIO 按键（G11/G12），不是 PM1 按键。
- PM1 按键是侧面的电源/复位键，通过 `btnGetState/btnGetFlag` 读取。
- `setDoubleOffDisable(true)` / `setSingleResetDisable(true)` 是**高危操作** — 误设后可能无法正常关机/复位。

### 5.7 M5PM1 vs M5PM1_Class

两个是**不同的类**，不可互换：

| | `M5PM1`（独立库） | `M5PM1_Class`（M5Unified） |
|---|---|---|
| 头文件 | `<M5PM1.h>` | `<utility/power/M5PM1_Class.hpp>` |
| 命名空间 | 全局 | `m5` |
| 实例化 | `M5PM1 pm1;` | `M5.Power.M5pm1`（已构造） |
| API 风格 | 返回值 `m5pm1_err_t` | 返回值 `bool` |

**若同时使用两者**：独立库 `M5PM1` 通过 `begin(&M5.In_I2c, ...)` 借用 M5Unified 的 I2C，不冲突。

### 5.8 StickS3 充电检测的硬件接线

在 StickS3 上，充电状态通过 **PM1 GPIO0** 读取（`Power_Class.cpp:1954`）：
```cpp
// StickS3: PM1_G0 is charging status input pin, low=charging / high=not charging
return M5pm1.getGPIOInput(M5PM1_Class::gpio0) ? is_charging_t::is_discharging : is_charging_t::is_charging;
```

这意味着 StickS3 初始化时 GPIO0 被配置为 `input`（非 `FUNC_OTHER`/NeoPixel）。如果你需要同时使用 NeoPixel（GPIO0 特殊功能）和充电检测，会冲突。

---

## 6. 版本兼容

| 项目 | 详情 |
|---|---|
| 本地版本 | **1.0.7**（`library.properties`） |
| 文档 README 标注 | 1.0.6（未同步更新，以 `library.properties` 为准） |
| 平台 | Arduino, ESP-IDF（双平台） |
| M5Unified 最低版本（官方要求） | >= 0.2.12（StickS3 Arduino 教程） |
| 架构 | esp32 |

---

## 7. 最小可编译示例

### 示例 1：读电量 + 使能 Grove/EXT_5V（独立库）

```cpp
#include <M5PM1.h>

M5PM1 pm1;

void setup() {
    Serial.begin(115200);

    // StickS3: SDA=47, SCL=48
    m5pm1_err_t err = pm1.begin(&Wire, M5PM1_DEFAULT_ADDR, 47, 48, M5PM1_I2C_FREQ_100K);
    if (err != M5PM1_OK) {
        Serial.printf("PM1 init failed: %d\n", err);
        while (1) delay(1000);
    }

    // 1. 使能 Grove/EXT_5V 输出（默认关闭！）
    pm1.setBoostEnable(true);
    Serial.println("EXT_5V enabled");

    // 2. 读电池电压
    uint16_t vbat = 0;
    if (pm1.readVbat(&vbat) == M5PM1_OK) {
        Serial.printf("Battery: %u mV (%.2f V)\n", vbat, vbat / 1000.0f);
    }

    // 3. 读电源来源
    m5pm1_pwr_src_t src;
    if (pm1.getPowerSource(&src) == M5PM1_OK) {
        Serial.printf("Power source: %s\n",
            src == M5PM1_PWR_SRC_5VIN ? "USB" :
            src == M5PM1_PWR_SRC_BAT  ? "Battery" : "Other");
    }
}

void loop() {
    delay(1000);
}
```

### 示例 2：M5Unified 方式（更简洁）

```cpp
#include <M5Unified.h>

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    // EXT_5V 使能
    M5.Power.setExtOutput(true);

    // 电量 0-100
    int level = M5.Power.getBatteryLevel();
    M5.Display.printf("Battery: %d%%\n", level);

    // 充电状态
    bool charging = M5.Power.isCharging();
    M5.Display.printf("Charging: %s\n", charging ? "yes" : "no");
}

void loop() {
    M5.update();
}
```

### 示例 3：定时唤醒（高级功能，必须独立库）

```cpp
#include <M5PM1.h>

M5PM1 pm1;

void setup() {
    Serial.begin(115200);
    pm1.begin(&Wire, 0x6E, 47, 48, 100000);

    // 检查唤醒源
    uint8_t wakeSrc = 0;
    pm1.getWakeSource(&wakeSrc, M5PM1_CLEAN_ALL);
    if (wakeSrc & M5PM1_WAKE_SRC_TIM) {
        Serial.println("Woken by timer!");
    }
}

void loop() {
    delay(5000);
    Serial.println("Going to sleep for 10 seconds...");
    pm1.timerSet(10, M5PM1_TIM_ACTION_POWERON);
    pm1.shutdown();
}
```

---

## 8. 参考链接

- GitHub: https://github.com/m5stack/M5PM1
- M5PM1 Datasheet (EN): https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf
- M5PM1 Datasheet (CN): 本地 `docs/M5PM1_Datasheet_CN.pdf`
- StickS3 Arduino 教程（含 M5PM1 章节）: https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- StickS3 官方文档: https://docs.m5stack.com/en/core/StickS3
- M5Unified: https://github.com/m5stack/M5Unified
