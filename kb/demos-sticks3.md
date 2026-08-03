# StickS3 官方 Arduino 文档(10 页)汇总

> 覆盖 docs.m5stack.com/en/arduino/m5sticks3/ 下全部 10 个官方页面,10/10 均可访问,无 404。
> 与设备 KB `kb/m5stick-s3.md` 配套使用;本文记录各页的代码模式与官方注意事项。

## 速览表

| 功能 | 库 | 核心 API | 备注 |
|---|---|---|---|
| 编译上传 (program) | M5Unified + M5GFX | Board Manager >= 3.2.5,板型 M5StickS3 | 按住复位键 2s(绿灯闪)进下载模式 |
| 低功耗电源 (m5pm1) | M5PM1 | `pm1.setLdoEnable()` / `pm1.timerSet()` / `pm1.shutdown()` | 多级电源 L0~L3B;EXT_5V 方向控制 |
| 电池 (battery) | M5Unified `Power_Class` | `M5.Power.getBatteryLevel()/getBatteryVoltage()/isCharging()` | 用 M5.Power,不是直接调 M5PM1 |
| 按键 (button) | M5Unified `Button_Class` | `M5.BtnA/B.isPressed()/wasPressed()` | loop 内必须 `M5.update()` |
| 显示 (display) | M5GFX | `M5.Display.fillCircle/fillRect/...` | ST7789P3 135×240 |
| IMU (imu) | M5Unified `IMU_Class` | `M5.Imu.update()` + `M5.Imu.getImuData()` | BMI270;三轴方向见官方图示 |
| 红外收发 (ir_nec) | ESP-IDF RMT driver | `rmt_new_tx_channel` / `rmt_new_rx_channel` | TX=G46 RX=G42;接收必须关功放 |
| 麦克风 (mic) | M5Unified `Mic_Class` | `M5.Mic.begin()/record()` | 与 Speaker 互斥,begin/end 切换 |
| 扬声器 (speaker) | M5Unified `Speaker_Class` | `M5.Speaker.tone(freq, ms)` | 电池供电音量建议 <75% |
| 睡眠唤醒 (wakeup) | M5Unified + M5PM1 | `M5.Power.timerSleep(s)` 或 `pm1.timerSet()+shutdown()` | PMIC 定时唤醒功耗更低 |

编译通用前提(各页一致):Board Manager >= 3.2.5,开发板选 `M5StickS3`,M5Unified >= 0.2.12,M5GFX >= 0.2.18;PMIC 相关页另需 M5PM1 库 >= 1.0.1。

---

## 编译与上传 (Program)

来源: https://docs.m5stack.com/en/arduino/m5sticks3/program

**用途**: Arduino IDE 环境搭建、进下载模式与上传第一个示例。

**关键流程**:
1. 安装 Arduino IDE → 安装 M5Stack Board Manager → 选开发板 `M5StickS3`。
2. 安装 `M5Unified` 与 `M5GFX` 库(按提示装齐依赖)。
3. 进下载模式:**按住侧面复位键约 2 秒,内部绿灯闪烁后松开**。
4. 选端口 → 打开库示例 `ScrollGraph` → 上传。

**官方注意事项**:
- FQBN 为 `m5stack:esp32:m5stack_sticks3`(已在 kb/m5stick-s3.md 核实)。
- 官方库入口: https://github.com/m5stack/M5Unified · https://github.com/m5stack/M5GFX

---

## 低功耗电源配置 (M5PM1)

来源: https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1

**用途**: 用 M5PM1 驱动库做多级电源开关(L0~L3B)、PMIC 睡眠、定时开机/关机/重启、IMU 唤醒。

### 多级电源结构

- 官方强调:L1~L3B 的电源输入**都来自 L0**,不是级联结构,各级可独立控制。
- M5PM1 上电后 L0/L1/L2 自动使能(`DCDC3V3_EN_PP`、`LDO3V3_EN_PP`、`CHG_EN_PP`);M5Unified 初始化时再使能 L3A/L3B。
- L0: 仅 M5PM1 供电(按键开关机仍可用)。
- L1: IMU 供电(`LDO3V3_EN_PP`);IMU INT1 接 M5PM1 PYG4,可翻转唤醒。
- L2/L3A: ESP32-S3、Grove、Hat、红外、按键上拉。ESP32-S3 睡眠时=L2,运行时=L3A;ESP32-S3 可让 M5PM1 进 sleep 来断掉自己的电(L2→L1/L0)。
- L3B: 全部外设(LCD 背光、MIC、SPK),开关为 M5PM1 `PYG2`。
- SPK 功放: 开关为 M5PM1 `PYG3`。

### API 速查

```cpp
// L1 (IMU 电源)
pm1.setLdoEnable(true);   // L1 ON
pm1.setLdoEnable(false);  // L1 OFF

// L1 保电后 M5PM1 睡眠(IMU 唤醒场景)
pm1.setLdoEnable(true);
pm1.ldoSetPowerHold(true);
pm1.setLedEnLevel(true);
pm1.shutdown();

// EXT_5V 方向控制(输出模式,恢复 Grove/Hat/IR 供电)
M5.Power.setExtOutput(true);  // EXT_5V OUTPUT
// M5.Power.setExtOutput(false); // EXT_5V INPUT

// L3B 开关(PYG2)
pm1.gpioSetFunc(M5PM1_GPIO_NUM_2, M5PM1_GPIO_FUNC_GPIO);
pm1.gpioSetMode(M5PM1_GPIO_NUM_2, M5PM1_GPIO_MODE_OUTPUT);
pm1.gpioSetDrive(M5PM1_GPIO_NUM_2, M5PM1_GPIO_DRIVE_PUSHPULL);
pm1.gpioSetOutput(M5PM1_GPIO_NUM_2, false);

// SPK 功放开关(PYG3)——两种等价写法
M5.Speaker.begin();  // 初始化 SPK 并使能功放
// M5.Speaker.end(); // 释放 SPK 并关功放
pm1.gpioSetFunc(M5PM1_GPIO_NUM_3, M5PM1_GPIO_FUNC_GPIO);
pm1.gpioSetMode(M5PM1_GPIO_NUM_3, M5PM1_GPIO_MODE_OUTPUT);
pm1.gpioSetDrive(M5PM1_GPIO_NUM_3, M5PM1_GPIO_DRIVE_PUSHPULL);
pm1.gpioSetOutput(M5PM1_GPIO_NUM_3, true);   // 开功放
// pm1.gpioSetOutput(M5PM1_GPIO_NUM_3, false); // 关功放

// PMIC 睡眠 / I2C 空闲自动睡眠
pm1.shutdown();                              // 手动睡眠,默认回退到 L0
m5pm1_err_t setI2cSleepTime(uint8_t seconds); // I2C 空闲自动睡眠

// PMIC 定时器(到期执行动作)
m5pm1_err_t timerSet(uint32_t seconds, m5pm1_tim_action_t action);
```

```cpp
typedef enum {
    M5PM1_TIM_ACTION_STOP = 0b000,     // Stop, no action
    M5PM1_TIM_ACTION_FLAG = 0b001,     // Set flag only
    M5PM1_TIM_ACTION_REBOOT = 0b010,   // System reboot
    M5PM1_TIM_ACTION_POWERON = 0b011,  // Power on
    M5PM1_TIM_ACTION_POWEROFF = 0b100  // Power off
} m5pm1_tim_action_t;
```

### PM1 初始化固定模板(所有 M5PM1 示例共用)

```cpp
#include <M5Unified.h>
#include <M5PM1.h>
#include <Wire.h>

M5PM1 pm1;

// setup() 内:
auto pin_num_sda = M5.getPin(m5::pin_name_t::in_i2c_sda);
auto pin_num_scl = M5.getPin(m5::pin_name_t::in_i2c_scl);
Wire.end();
Wire.begin(pin_num_sda, pin_num_scl, 100000U);

m5pm1_err_t err = pm1.begin(&Wire, M5PM1_DEFAULT_ADDR, pin_num_sda, pin_num_scl, M5PM1_I2C_FREQ_100K);
```

### 定时开机/关机模式

```cpp
// 10s 后自动开机(先设定时再 shutdown)
pm1.timerSet(10, M5PM1_TIM_ACTION_POWERON);
pm1.shutdown();

// 10s 后 PMIC 断电(可用电源键再开机)
pm1.timerSet(10, M5PM1_TIM_ACTION_POWEROFF);
```

### IMU 唤醒 M5PM1(L1 模式)

配置 BMI270 any-motion 中断映射到 INT1(接 M5PM1 PYG4),再保电睡眠:

```cpp
// PM1 侧(PYG4 下降沿唤醒)
pm1.gpioSetWakeEnable(M5PM1_GPIO_NUM_4, true);
pm1.gpioSetWakeEdge(M5PM1_GPIO_NUM_4, M5PM1_GPIO_WAKE_FALLING);

// BMI270 侧(SparkFun_BMI270_Arduino_Library)
int8_t ret = imu.enableFeature(BMI2_ANY_MOTION);
// 可选灵敏度配置:
// bmi2_sens_config config;
// config.type = BMI2_ANY_MOTION;
// config.cfg.any_motion.threshold = 0xA0; // 1LSB=0.48mg,默认83mg,越小越灵敏
// config.cfg.any_motion.duration = 0x0A;  // 1LSB=20ms,默认100ms
// ret |= imu.setConfig(config);
bmi2_int_pin_config intPinConfig;
intPinConfig.pin_type = BMI2_INT1;
intPinConfig.int_latch = BMI2_INT_NON_LATCH;
intPinConfig.pin_cfg[0].lvl = BMI2_INT_ACTIVE_LOW;
intPinConfig.pin_cfg[0].od = BMI2_INT_PUSH_PULL;
intPinConfig.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
intPinConfig.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;
ret |= imu.setInterruptPinConfig(intPinConfig);
ret |= imu.mapInterruptToPin(BMI2_ANY_MOTION_INT, BMI2_INT1);

// 保电 + 睡眠
pm1.setLdoEnable(true);
pm1.ldoSetPowerHold(true);
pm1.setLedEnLevel(true);
pm1.shutdown();
```

唤醒后 M5PM1 重走 L0/L1/L2 上电时序,ESP32-S3 冷启动(从头跑 setup)。

### IMU 链式唤醒 ESP32-S3(经 PYG1_IRQ → G13)

M5PM1 `PYG1_IRQ` 硬件连到 ESP32-S3 的 **G13**。步骤:PYG4 配输入(接 IMU INT1) → PYG1_IRQ 配 IRQ 输出 → ESP32-S3 睡眠并设 G13 为唤醒脚。

```cpp
pm1.irqClearGpioAll();
pm1.irqClearSysAll();
pm1.irqClearBtnAll();
pm1.irqSetGpioMaskAll(M5PM1_IRQ_MASK_ENABLE);
pm1.irqSetSysMaskAll(M5PM1_IRQ_MASK_ENABLE);
pm1.irqSetBtnMaskAll(M5PM1_IRQ_MASK_ENABLE);

pm1.irqSetGpioMask(M5PM1_IRQ_GPIO4, M5PM1_IRQ_MASK_DISABLE);
pm1.gpioSetMode(M5PM1_GPIO_NUM_4, M5PM1_GPIO_MODE_INPUT);
pm1.gpioSetPull(M5PM1_GPIO_NUM_4, M5PM1_GPIO_PULL_UP);

pm1.gpioSetMode(M5PM1_GPIO_NUM_1, M5PM1_GPIO_MODE_OUTPUT);
pm1.gpioSetDrive(M5PM1_GPIO_NUM_1, M5PM1_GPIO_DRIVE_PUSHPULL);
pm1.gpioSetFunc(M5PM1_GPIO_NUM_1, M5PM1_GPIO_FUNC_IRQ);

// ESP32-S3 侧二选一:运行态中断
pinMode(GPIO_NUM_13, INPUT_PULLUP);
attachInterrupt(GPIO_NUM_13, pm1_irq_handler, FALLING);
// 或深睡唤醒:
// esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 0);  // 0 = Low
// rtc_gpio_pullup_en(GPIO_NUM_13);
// esp_deep_sleep_start();
```

### 官方注意事项

1. **EXT_5V 方向**:默认输入模式,可经 Grove / Hat2-Bus EXT_5V / 5VIN 灌入 5V;配为输出模式时只能从 USB 或 Hat 5VIN 供电,禁止从其他输出口反灌,否则短路烧机。
2. M5Unified 默认初始化 **关闭 EXT_5V_EN**,Grove/Hat EXT_5V/IR 收发均无电;无外部 5V 输入时须 `M5.Power.setExtOutput(true)`。
3. 接外设传感器或用红外前,先确认 EXT_5V 已供电。
4. **用 IR 接收时必须关 SPK 功放**(见 ir_nec 节)。
5. I2C 空闲睡眠唤醒时,第一次通信会失败(用于唤醒 M5PM1),有效通信发生在下一笔。
6. BMI270 驱动用 [SparkFun_BMI270_Arduino_Library](https://github.com/sparkfun/SparkFun_BMI270_Arduino_Library),地址默认 0x68(`BMI2_I2C_PRIM_ADDR`)。

---

## 电池 (Battery)

来源: https://docs.m5stack.com/en/arduino/m5sticks3/battery

**用途**: 读取充电状态、电量百分比、电池电压。

**关键代码模式**:

```cpp
bool isCharging = M5.Power.isCharging();
int vol_per = M5.Power.getBatteryLevel();
int vol = M5.Power.getBatteryVoltage();
```

**API 速查**(M5Unified `Power_Class`):
- `M5.Power.isCharging()` → bool,是否在充电
- `M5.Power.getBatteryLevel()` → int,百分比
- `M5.Power.getBatteryVoltage()` → int,mV
- 完整 API: https://docs.m5stack.com/en/arduino/m5unified/power_class

**官方注意事项 / 与 M5PM1 的 API 分工**:
- 电池状态读取走 **M5.Power**(M5Unified 封装),官方 battery 示例不直接调 M5PM1;M5PM1 库面向电源轨开关/睡眠/定时器,两套 API 各司其职,读电量不需要初始化 `pm1`。
- 官方示例里 `M5.Lcd.printf("Bat_voltage: %d%mV", vol)` 的 `%mV` 为原文笔误(应为 `mV` 字面拼接),照抄时注意。

---

## 按键 (Button)

来源: https://docs.m5stack.com/en/arduino/m5sticks3/button

**用途**: 读取 BtnA/BtnB 状态并显示。

**关键代码模式**:

```cpp
void loop() {
    M5.update();  // 必须
    if (M5.BtnA.isPressed() || M5.BtnB.isPressed()) { /* ... */ }
    vTaskDelay(1);
}
```

**API 速查**(M5Unified `Button_Class`):
- `M5.BtnA.isPressed()` / `M5.BtnA.wasPressed()`(BtnB 同)
- 完整 API: https://docs.m5stack.com/en/arduino/m5unified/button_class

**官方注意事项**:
- 主循环必须包含 `M5.update()` 且尽量少阻塞,否则按键状态更新不及时。
- StickS3 的 BtnA/BtnB 对应硬件 KEY1(G11)/KEY2(G12)(见 kb/m5stick-s3.md)。

---

## 显示 (Display)

来源: https://docs.m5stack.com/en/arduino/m5sticks3/display

**用途**: M5GFX 绘图示例(随机圆/方块)。

**关键代码模式**:

```cpp
auto cfg = M5.config();
M5.begin(cfg);
int textsize = M5.Display.height() / 60;
if (textsize == 0) textsize = 1;
M5.Display.setTextSize(textsize);
M5.Display.clear(TFT_WHITE);

// 循环中:
M5.Display.fillCircle(x, y, r, c);
// 也可传 LovyanGFX* 给自定义绘制函数:
void draw_function(LovyanGFX* gfx) { gfx->fillRect(x - r, y - r, r * 2, r * 2, c); }
draw_function(&M5.Display);
```

**API 速查**: `M5.Display` 即 LovyanGFX 实例,`width()/height()/clear()/fillCircle()/fillRect()/setTextSize()/setRotation()/setFont()/drawString()`;更多见 https://github.com/m5stack/M5GFX 源码。

**官方注意事项**: 屏幕 135×240(ST7789P3);`M5.Lcd` 与 `M5.Display` 在官方各示例中混用,等价。

---

## IMU 姿态传感器 (IMU)

来源: https://docs.m5stack.com/en/arduino/m5sticks3/imu

**用途**: 读取 BMI270 三轴加速度/角速度并显示。

**关键代码模式**:

```cpp
auto imu_update = M5.Imu.update();
if (imu_update) {
    auto ImuData = M5.Imu.getImuData();
    ImuData.accel.x; ImuData.accel.y; ImuData.accel.z;
    ImuData.accel.value;  // 3values array [0]=x / [1]=y / [2]=z
    ImuData.gyro.x;  ImuData.gyro.y;  ImuData.gyro.z;
    ImuData.gyro.value;
}
```

**API 速查**(M5Unified `IMU_Class`):
- `M5.Imu.update()` → bool,有新数据返回 true
- `M5.Imu.getImuData()` → `imu_data_t`,含 `accel.{x,y,z,value[3]}` 与 `gyro.{x,y,z,value[3]}`
- 完整 API: https://docs.m5stack.com/en/arduino/m5unified/imu_class

**官方注意事项**:
- 三轴方向以官方图示为准: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/IMU-StickS3.jpg
- 中断唤醒等高级玩法不在本页,见 M5PM1 页(IMU 唤醒 M5PM1 / 链式唤醒 ESP32-S3)。

---

## 红外收发 (IR NEC)

来源: https://docs.m5stack.com/en/arduino/m5sticks3/ir_nec

**用途**: 用 ESP32-S3 RMT 外设实现 NEC 协议红外发送、接收解码、接收回显(echo-back)。

**引脚**: IR_TX = **G46**,IR_RX = **G42**。

### 官方注意事项(重要,置于代码前)

1. **红外接收解码必须用 ESP32 RMT 外设**,不支持 GPIO 软件解码。
2. **使用 IR 接收时必须关闭扬声器功放**:`M5.Speaker.end();`(或 M5PM1 PYG3 API),否则无法正确接收。
3. 收发时发射端正对接收端,**距离 > 30cm**,太近会接收异常;官方建议借助反射面把发射信号反射给接收端以获得最佳效果。
4. 发送与接收都要先 `M5.Power.setExtOutput(true, m5::ext_none)` 给 IR 模块供电(M5Unified 默认关 EXT_5V)。
5. NEC 地址 ≤ 0x00FF 时按标准格式发送(8-bit 地址 + 反码),接收端解析出的地址与发送端"不完全相同";> 0x00FF 时按扩展格式发送完整 16-bit 地址,收发一致。

### 发送端关键配置(RMT TX)

```cpp
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

#define IR_SEND_PIN    46
#define IR_CARRIER_FREQ_HZ  38000
#define IR_DUTY_CYCLE       0.33

rmt_tx_channel_config_t tx_chan_config = {
    .gpio_num = (gpio_num_t)IR_SEND_PIN,
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 1000000,  // 1 us per tick
    .mem_block_symbols = 64,
    .trans_queue_depth = 4,
    .flags = { .invert_out = false, .with_dma = false }
};
ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &tx_chan));

rmt_carrier_config_t carrier_cfg = {
    .frequency_hz = IR_CARRIER_FREQ_HZ,
    .duty_cycle = IR_DUTY_CYCLE,
    .flags = { .polarity_active_low = false }
};
ESP_ERROR_CHECK(rmt_apply_carrier(tx_chan, &carrier_cfg));

rmt_copy_encoder_config_t encoder_config = {};
ESP_ERROR_CHECK(rmt_new_copy_encoder(&encoder_config, &copy_encoder));
ESP_ERROR_CHECK(rmt_enable(tx_chan));
```

NEC 时序常量(µs): `NEC_HEADER_MARK 9000`、`NEC_HEADER_SPACE 4500`、`NEC_BIT_MARK 560`、`NEC_BIT_0_SPACE 560`、`NEC_BIT_1_SPACE 1690`、`NEC_REPEAT_MARK 9000`、`NEC_REPEAT_SPACE 2250`。32-bit 帧(LSB 在前):bit 0-15 地址域,bit 16-23 命令,bit 24-31 命令反码。发送:

```cpp
rmt_transmit_config_t tx_config = { .loop_count = 0, .flags = { .eot_level = 0 } };
esp_err_t ret = rmt_transmit(tx_chan, copy_encoder, symbols,
                             symbol_count * sizeof(rmt_symbol_word_t), &tx_config);
if (ret == ESP_OK) ret = rmt_tx_wait_all_done(tx_chan, 1000);
```

地址组装(标准/扩展自适应):

```cpp
uint32_t NECRaw(uint16_t address, uint8_t command) {
    uint16_t nec_addr;
    if (address <= 0x00FF) {
        uint8_t addr8 = address & 0xFF;
        nec_addr = ((uint16_t)(~addr8) << 8) | addr8;  // 标准: 8bit + 反码
    } else {
        nec_addr = address;  // 扩展: 完整 16bit
    }
    uint32_t raw = 0;
    raw |= (uint32_t)nec_addr;
    raw |= (uint32_t)command << 16;
    raw |= (uint32_t)(~command) << 24;
    return raw;
}
```

### 接收端关键配置(RMT RX)

```cpp
#include "driver/rmt_rx.h"

#define IR_RECEIVE_PIN 42

// setup() 开头:
M5.begin();
M5.Speaker.end();  // 必须!关功放避免 IR RX 干扰
M5.Power.setExtOutput(true, m5::ext_none);  // 给 IR 接收模块供电

rmt_rx_channel_config_t rx_chan_config = {
    .gpio_num = (gpio_num_t)IR_RECEIVE_PIN,
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 1000000,
    .mem_block_symbols = 128,
};
ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &rx_chan));
rmt_rx_event_callbacks_t cbs = { .on_recv_done = rmt_rx_done_callback };
ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &cbs, NULL));
ESP_ERROR_CHECK(rmt_enable(rx_chan));

// 开始一次接收(每次收到一帧后需重新 arm):
rmt_receive_config_t receive_config = {
    .signal_range_min_ns = 1000,
    .signal_range_max_ns = 20000000
};
ESP_ERROR_CHECK(rmt_receive(rx_chan, rx_raw_symbols, sizeof(rx_raw_symbols), &receive_config));
```

RX 完成回调(ISR 上下文,返回 true 请求延迟处理):

```cpp
bool rmt_rx_done_callback(rmt_channel_handle_t channel,
                          const rmt_rx_done_event_data_t *edata, void *user_data) {
    rx_symbol_num = edata->num_symbols;
    rx_done = true;
    return true;
}
```

NEC 解码要点(官方 `decodeNEC`): 帧头 LOW >8000 且 HIGH >4000 为数据帧;LOW >8000 且 2000<HIGH<3000 为 repeat 帧;每 bit mark 须在 300~800µs,space >1000µs 判 1;最后校验 `cmd ^ cmd_inv == 0xFF`。

### 接收回显 (echo-back)

第三份示例同时开 RX + TX 通道:收到一帧后 dump 每个 symbol 的电平时长(µs)与 hex 到串口,再用 `rmt_transmit` 原样转发。缓冲用 `static rmt_symbol_word_t rx_raw_symbols[256]`(单收示例用 64)。TX 通道 `mem_block_symbols` 用 128。

---

## 麦克风 (Mic)

来源: https://docs.m5stack.com/en/arduino/m5sticks3/mic

**用途**: 录音(环形缓冲)+ 按键回放,演示 Mic/Speaker 互斥切换。

### Mic/Speaker 互斥写法(核心模式)

```cpp
// 进录音:先关 Speaker 再开 Mic
M5.Speaker.setVolume(200);
M5.Speaker.end();   // 麦克风与扬声器不能同时用
M5.Mic.begin();

// 回放前:等录音完成 → 关 Mic → 开 Speaker
if (M5.Speaker.isEnabled()) {
    while (M5.Mic.isRecording()) { delay(1); }
    M5.Mic.end();
    M5.Speaker.begin();
    M5.Speaker.playRaw(&rec_data[start_pos], record_size - start_pos,
                       record_samplerate, false, 1, 0);
    do { delay(1); M5.update(); } while (M5.Speaker.isPlaying());
    // 播完回到录音
    M5.Speaker.end();
    M5.Mic.begin();
}
```

### API 速查(M5Unified `Mic_Class` / `Speaker_Class`)

- `M5.Mic.begin()/end()/isEnabled()/isRecording()`
- `M5.Mic.record(int16_t* data, size_t length, uint32_t samplerate)` → bool;示例参数 256 样本/次、18000Hz
- 录音缓冲用 `heap_caps_malloc(record_size * sizeof(int16_t), MALLOC_CAP_8BIT)` 分配
- `M5.Speaker.playRaw(...)`、`M5.Speaker.isPlaying()/setVolume()/begin()/end()/isEnabled()`
- 完整 API: https://docs.m5stack.com/en/arduino/m5unified/mic_class · https://docs.m5stack.com/en/arduino/m5unified/speaker_class

**官方注意事项**: 官方示例三次注释强调"麦克风与扬声器不能同时使用",切换顺序固定为 `end()` 旧的 → `begin()` 新的。

---

## 扬声器 (Speaker)

来源: https://docs.m5stack.com/en/arduino/m5sticks3/speaker

**用途**: 定时发出两种频率的提示音。

**关键代码模式**(整页示例,<40 行):

```cpp
#include "M5Unified.h"

void setup() {
    M5.begin();
    M5.Lcd.setRotation(1);
    M5.Lcd.setTextDatum(middle_center);
    M5.Lcd.setTextFont(&fonts::FreeMonoBold9pt7b);
    M5.Lcd.clear();
    M5.Lcd.drawString("Speaker", M5.Lcd.width() / 2, M5.Lcd.height() / 2);
    delay(100);
}

void loop() {
    M5.Speaker.tone(7000, 100);  // frequency, duration
    delay(1000);
    M5.Speaker.tone(4000, 200);  // frequency, duration
    delay(1000);
}
```

**API 速查**(M5Unified `Speaker_Class`):
- `M5.Speaker.tone(frequency, duration_ms)`
- 完整 API: https://docs.m5stack.com/en/arduino/m5unified/speaker_class

**官方注意事项**(来自设备主页,与 kb/m5stick-s3.md 一致): 电池供电下音量建议 < 75%,大功耗可能意外重启;`M5.Speaker.begin()` 会同时使能 M5PM1 PYG3 功放(见 M5PM1 页)。

---

## 睡眠唤醒 (Wakeup)

来源: https://docs.m5stack.com/en/arduino/m5sticks3/wakeup

**用途**: 两种睡眠唤醒方案 —— M5Unified `timerSleep`(ESP32-S3 侧)与 M5PM1 PMIC 定时唤醒(整机断电,功耗更低)。

### 方案一: M5.Power.timerSleep(整页示例,<40 行)

```cpp
#include "M5Unified.h"

void setup(void) {
  M5.begin();

  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextFont(&fonts::FreeMonoBoldOblique9pt7b);
  M5.Display.setRotation(1);

  M5.Display.drawString("BtnA to Sleep 5s", M5.Display.width() / 2, M5.Display.height() / 2);
}

void loop(void) {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    M5.Display.clear();
    M5.Power.timerSleep(5);//sec
  }
}
```

### 方案二: PMIC 定时唤醒(推荐低功耗)

需 M5PM1 库 >= 1.0.1。该模式可切断 ESP32-S3 等外设供电,功耗更低;唤醒是冷启动(重新跑 setup)。

```cpp
// PM1 初始化模板见 "低功耗电源配置 (M5PM1)" 节
// 按键按下后:
pm1.timerSet(10, M5PM1_TIM_ACTION_POWERON);  // 10s 后上电
pm1.shutdown();                               // PMIC 睡眠(回 L0)
```

**API 速查**:
- `M5.Power.timerSleep(int seconds)` —— M5Unified `Power_Class`,文档: https://docs.m5stack.com/en/arduino/m5unified/power_class
- `pm1.timerSet(uint32_t seconds, m5pm1_tim_action_t action)` + `pm1.shutdown()` —— M5PM1 库,动作枚举见 M5PM1 节
- 更多 PMIC 教程: https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1

**官方注意事项 / 唤醒方式全景**:
- 按键唤醒属于 M5PM1 L0 级原生能力(电源键开机),无需代码;软件层面本页只给定时唤醒。
- IMU 翻转唤醒(M5PM1 PYG4)与 IMU 链式唤醒 ESP32-S3(PYG1_IRQ → G13)在 M5PM1 页,不在本页。
- 定时动作用 `M5PM1_TIM_ACTION_POWERON` 是"关机后再开机";若只想 ESP32-S3 睡眠定时醒,用方案一。
