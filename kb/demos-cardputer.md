# Cardputer / Cardputer-Adv 官方 Arduino Demo 页汇总

> 来源：docs.m5stack.com 官方 Cardputer Arduino 文档（共 10 页），全部页面均适用于 Cardputer 与 Cardputer-Adv（IMU 页除外，仅 Adv）。
> 交叉引用：设备硬件细节见 `kb/cardputer-adv.md`；M5Cardputer 库完整 API 见 `kb/lib-m5cardputer.md`；M5Unified 类（Power/Button/Imu/Speaker/Mic）见 `kb/lib-m5unified.md`；M5GFX 绘制 API 见 `kb/lib-m5gfx.md`。本文件只记录各 demo 页独有信息与官方注意事项，不重复完整类参考。

## 速览表

| 功能 | 库 | 核心 API | 备注 |
|---|---|---|---|
| 编译/上传 (program) | M5Cardputer | 板型 `M5Cardputer`（无 Adv 专属板型） | 下载模式：开关 OFF + 按住 G0 + 插 USB |
| 电量 (battery) | M5Unified `Power_Class` | `Power.getBatteryLevel()` / `getBatteryVoltage()` | 无 PMU，G10 ADC；`isCharging()` 恒无意义 |
| 按钮 (button) | M5Unified `Button_Class` | `BtnA.wasPressed()` / `wasReleased()` | 仅顶部 G0 一键；G0 也用于进下载模式 |
| 屏幕 (display) | M5GFX (LovyanGFX) | `Display.fillCircle()` 等 | ST7789V2 240×135 |
| IMU (imu) | M5Unified `Imu_Class` | `M5.Imu.update()` / `getImuData()` | **仅 Cardputer-Adv**（BMI270）；用 `M5.Imu` 而非 `M5Cardputer` |
| 红外 (ir_nec) | Arduino-IRremote | `IrSender.sendNEC()` | G44 仅 TX，无 RX 接收 |
| 键盘 (keyboard) | M5Cardputer `Keyboard_Class` | `isChange()` / `isPressed()` / `keysState()` | loop 必须持续 `M5Cardputer.update()` |
| 麦克风 (mic) | M5Unified `Mic_Class` | `Mic.record()` | 与 Speaker 互斥，必须 end/begin 切换 |
| microSD (sdcard) | Arduino 内置 `SD` + `SPI` | `SD.begin(CS, SPI, 25000000)` | 引脚 SCK40/MISO39/MOSI14/CS12；需 FAT32 |
| 扬声器 (speaker) | M5Unified `Speaker_Class` | `Speaker.tone()` / `playRaw()` | Adv 插 3.5mm 耳机自动切换输出通道 |

---

## Program（编译与上传）

来源：https://docs.m5stack.com/en/arduino/m5cardputer/program

### 用途
Cardputer 与 Cardputer-Adv 通用的 Arduino 开发环境搭建与首程序上传流程。

### 关键代码模式
- Board Manager 选 `M5Cardputer`（Adv 也用此板型，无专属条目）。
- 安装 `M5Cardputer` 驱动库并按提示下载全部依赖。
- 官方验证示例：`M5Cardputer` 库示例 `Basic -> display`。

### API 速查
- FQBN：`m5stack:esp32:m5stack_cardputer`（详见 `kb/cardputer-adv.md`）。
- 下载模式操作：顶部电源开关拨到 **OFF** → 按住旁边的 **G0 按钮** → 用 USB-C 连接电脑 → 松开 G0，进入 download mode。

### 官方注意事项
- 无特殊坑；端口选择在上传前完成。

---

## Battery（电量状态）

来源：https://docs.m5stack.com/en/arduino/m5cardputer/battery

### 用途
读取电池电量百分比与电压，适用于 Cardputer 与 Cardputer-Adv。

### 关键代码模式

```cpp
#include <M5Cardputer.h>

void setup() {
  M5Cardputer.begin();
}

void loop() {
  M5Cardputer.update();
  bool isCharging = M5Cardputer.Power.isCharging();
  int batteryLevel   = M5Cardputer.Power.getBatteryLevel();    // 0 - 100 %
  int batteryVoltage = M5Cardputer.Power.getBatteryVoltage();  // 单位 mV
  delay(1000);
}
```

### API 速查

| API | 签名 | 返回 |
|---|---|---|
| 充电状态 | `bool Power.isCharging()` | **硬件上无意义，见坑** |
| 电量百分比 | `int Power.getBatteryLevel()` | 0–100 |
| 电池电压 | `int Power.getBatteryVoltage()` | mV |

驱动为 M5Unified 的 `Power_Class`，完整 API 见 `kb/lib-m5unified.md`（Power 部分）。

### 官方注意事项
1. **Cardputer 系列无 PMU**（电量经 G10 ADC 采样，见 `kb/cardputer-adv.md`）。官方原文："Due to hardware limitations, Cardputer and Cardputer-Adv **cannot read battery charging status or battery current information**." → `isCharging()` 返回的充电状态不可用，也没有电池电流 API。
2. **充电必须开机**：连接电脑/电源充电时，顶部电源开关必须拨到 ON；否则电池被断开，设备仅靠外部供电运行。
3. 编译要求：board manager ≥ 3.2.3，M5Cardputer ≥ 1.1.1，M5Unified ≥ 0.2.10（比其他页要求更高）。

---

## Button（按钮）

来源：https://docs.m5stack.com/en/arduino/m5cardputer/button

### 用途
读取顶部 G0 按钮的按下/释放事件，适用于 Cardputer 与 Cardputer-Adv。

### 关键代码模式

```cpp
#include "M5Cardputer.h"

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);
}

void loop() {
  M5Cardputer.update();
  if (M5Cardputer.BtnA.wasPressed())  { /* 按下沿 */ }
  if (M5Cardputer.BtnA.wasReleased()) { /* 释放沿 */ }
}
```

### API 速查
- `M5Cardputer.BtnA` 是 M5Unified `Button_Class`（`M5.getButton(0)`，G0 引脚）。完整边沿/长按 API（`wasClicked` / `wasHold` / `isPressed`）见 `kb/lib-m5cardputer.md` §4.6 与 `kb/lib-m5unified.md`。
- 官方明确：**只有顶部 G0 按钮**适用本页 API；正面 56 键键盘走 [Keyboard](#keyboard键盘) 页 API。

### 官方注意事项
1. **G0 的双重身份**：G0 既是用户程序里的 BtnA，也是进下载模式的 Boot 键（按住 G0 再上电 = download mode）。设备复位/上电瞬间若程序逻辑依赖 BtnA 初值，注意此时用户可能正按着它。
2. 必须 loop 中持续 `M5Cardputer.update()`，减少阻塞操作，否则按钮状态变化漏检。

---

## Display（屏幕）

来源：https://docs.m5stack.com/en/arduino/m5cardputer/display

### 用途
LCD 图形绘制，适用于 Cardputer 与 Cardputer-Adv。

### 关键代码模式

```cpp
#include "M5Cardputer.h"

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);
  int textsize = M5Cardputer.Display.height() / 60;
  if (textsize == 0) textsize = 1;
  M5Cardputer.Display.setTextSize(textsize);
}

void loop() {
  M5Cardputer.Display.fillCircle(x, y, r, color);
}

// 绘制函数可写成接收 LovyanGFX* 的通用形式：
void draw_function(LovyanGFX* gfx) {
  gfx->fillRect(x - r, y - r, r * 2, r * 2, c);
}
```

### API 速查
- `M5Cardputer.Display` 即 M5GFX（LovyanGFX）实例；ST7789V2，240×135。
- 完整绘制 API 见 `kb/lib-m5gfx.md`；屏幕硬件引脚见 `kb/cardputer-adv.md`。

### 官方注意事项
- 官方示例展示了一个有用技巧：绘制辅助函数写成 `void fn(LovyanGFX* gfx)` 签名，可复用于 Display 与 M5Canvas sprite。
- 无专属坑。

---

## IMU（六轴运动传感器）

来源：https://docs.m5stack.com/en/arduino/m5cardputer/imu

### 用途
读取 BMI270 六轴（加速度 + 陀螺仪）数据。**仅 Cardputer-Adv 支持，初代 Cardputer 无 IMU。**

### 关键代码模式

```cpp
#include <M5Cardputer.h>

m5::imu_data_t imuData;

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);  // enableKeyboard
}

void loop() {
  // 注意：用 M5.Imu，不是 M5Cardputer（M5Cardputer 无 Imu 快捷引用）
  M5.Imu.update();
  imuData = M5.Imu.getImuData();
  // imuData.accel.x/y/z, imuData.gyro.x/y/z
  delay(100);
}
```

### API 速查

| API | 签名 | 说明 |
|---|---|---|
| 更新数据 | `bool M5.Imu.update()` | 每帧调用 |
| 取数据 | `m5::imu_data_t M5.Imu.getImuData()` | 结构含 `accel`/`gyro`（各有 x/y/z float）等 |

数据类型 `m5::imu_data_t` 与完整 `Imu_Class` API 见 `kb/lib-m5unified.md`（Imu 部分）。BMI270 挂内部 I2C（G8/G9），与音频 codec、键盘芯片共总线（见 `kb/cardputer-adv.md`）。

### 官方注意事项
1. **机型限制**：官方原文 "only applicable to Cardputer-Adv and not supported on Cardputer"。代码需跨机型运行时用 `M5.getBoard()` 或 `M5.Imu.isEnabled()` 类判断兜底。
2. **示例注释强调**："Use M5 for Imu class, not M5Cardputer" —— IMU 没有挂到 M5Cardputer 快捷对象上，必须走 `M5.Imu`。
3. 页面附三轴方向示意图（IMU-Cardputer-Adv.jpg），姿态解算时注意轴向定义以图为准。

---

## IR NEC（红外发射）

来源：https://docs.m5stack.com/en/arduino/m5cardputer/ir_nec

### 用途
通过 G44 红外发射管发送 NEC 协议码，适用于 Cardputer 与 Cardputer-Adv。**只有发射，无红外接收（RX）硬件。**

### 关键代码模式

```cpp
// 编译期开关（在 #include <IRremote.hpp> 之前定义）：
#define DISABLE_CODE_FOR_RECEIVER   // 砍掉接收代码，省 450B flash + 269B RAM
#define SEND_PWM_BY_TIMER
#define IR_TX_PIN 44                // 必须先定义引脚宏

#include "M5Cardputer.h"
#include <IRremote.hpp>             // https://github.com/Arduino-IRremote/Arduino-IRremote

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  IrSender.begin(DISABLE_LED_FEEDBACK);
  IrSender.setSendPin(IR_TX_PIN);
}

void loop() {
  IrSender.sendNEC(0x1111, sCommand, sRepeats);  // address, command, repeats
  // 其他协议同理：IrSender.sendOnkyo(0x1111, 0x2223, sRepeats);
}
```

### API 速查

| API | 签名 | 说明 |
|---|---|---|
| 初始化 | `IrSender.begin(DISABLE_LED_FEEDBACK)` | 禁用反馈 LED |
| 设引脚 | `IrSender.setSendPin(pin)` | Cardputer 固定 G44 |
| NEC 发送 | `IrSender.sendNEC(uint16_t address, uint8_t command, uint_fast8_t repeats)` | 16 位地址标准 NEC |

### 官方注意事项
1. **G44 只有 TX**：Cardputer 无红外接收管，不要尝试 `IrReceiver`；官方示例用 `DISABLE_CODE_FOR_RECEIVER` 宏直接裁掉接收功能省内存。
2. 宏定义顺序：`#define IR_TX_PIN 44` 与 `DISABLE_CODE_FOR_RECEIVER` / `SEND_PWM_BY_TIMER` 必须在 `#include <IRremote.hpp>` **之前**。
3. M5Cardputer 库不封装 IR（见 `kb/lib-m5cardputer.md` §4.8），直接依赖第三方 Arduino-IRremote。
4. **Adv 专属坑**：G44 IR TX 与 RGB LED 电源使能 G38 无关，但 Adv 上要点 RGB LED 需先拉高 G38（见 `kb/cardputer-adv.md`），红外发射本身无此要求。

---

## Keyboard（键盘）

来源：https://docs.m5stack.com/en/arduino/m5cardputer/keyboard

### 用途
读取正面 56 键（4×14）键盘输入，适用于 Cardputer（74HC138 矩阵）与 Cardputer-Adv（TCA8418 芯片），库自动适配。完整类参考见 `kb/lib-m5cardputer.md` §4.3，此处记录官方 demo 页给出的 API 表与新信息。

### 关键代码模式（官方输入回显模式）

```cpp
#include "M5Cardputer.h"

M5Canvas canvas(&M5Cardputer.Display);
String data = "> ";

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);  // enableKeyboard
  M5Cardputer.Display.setRotation(1);
  canvas.setTextScroll(true);
  canvas.createSprite(M5Cardputer.Display.width() - 8, M5Cardputer.Display.height() - 36);
}

void loop() {
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange()) {
    if (M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
      for (auto i : status.word) data += i;          // 打印字符
      if (status.del)   data.remove(data.length() - 1); // 退格
      if (status.enter) { /* 提交 data */ data = "> "; }
    }
  }
}
```

### API 速查（官方 demo 页版）

| API | 签名 | 说明 |
|---|---|---|
| 初始化 | `void begin()` | 一般不经手；`M5Cardputer.begin(cfg, enableKeyboard=true)` 时一并初始化 |
| 状态变化 | `bool isChange()` | 任意键按下或释放时触发 |
| 按下键数 | `uint8_t isPressed()` | 当前按下的键数（非 bool！） |
| 单键查询 | `bool isKeyPressed(char c)` | 参数可为 `'A'`/`'a'`/`'1'`/`','`/`' '` 或 **`KEY_LEFT_SHIFT`、`KEY_BACKSPACE`、`KEY_ENTER`、`KEY_FN`** 等特殊键常量 |
| 坐标取值 | `uint8_t getKey(Point2D_t keyCoor)` | x∈[0,13]（0=最左列），y∈[0,3]（0=最上行）；返回十进制 ASCII |

新增信息（相对 `kb/lib-m5cardputer.md`）：官方确认 `isKeyPressed` 接受 `KEY_LEFT_SHIFT` / `KEY_BACKSPACE` / `KEY_ENTER` / `KEY_FN` 等特殊键常量，这些常量定义在 M5Cardputer 库 `src/utility/Keyboard/` 源码中（页面给出源码链接：https://github.com/m5stack/M5Cardputer/tree/master/src/utility ）。

### 官方注意事项
1. 必须 loop 中持续 `M5Cardputer.update()`，减少阻塞操作，否则键盘变化漏采（与 button 页同一告诫）。
2. 官方示例 UI 模式值得复用：M5Canvas 做可滚动文本区（`setTextScroll(true)`），Display 底部画固定输入行，`status.word`/`del`/`enter` 三字段即够实现完整行编辑。

---

## Mic（麦克风）

来源：https://docs.m5stack.com/en/arduino/m5cardputer/mic

### 用途
录制 PCM 音频并显示实时波形，缓冲最近几秒录音；适用于 Cardputer 与 Cardputer-Adv。

### 关键代码模式（环形缓冲录音）

```cpp
#include <M5Cardputer.h>

static constexpr const size_t record_number     = 256;    // 环形缓冲块数
static constexpr const size_t record_length     = 240;    // 每块采样数
static constexpr const size_t record_size       = record_number * record_length;
static constexpr const size_t record_samplerate = 17000;
static int16_t *rec_data;

void setup(void) {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);
  rec_data = (int16_t*)heap_caps_malloc(record_size * sizeof(int16_t), MALLOC_CAP_8BIT);
  memset(rec_data, 0, record_size * sizeof(int16_t));
  M5Cardputer.Speaker.setVolume(255);
  M5Cardputer.Speaker.end();   // 麦克风与扬声器互斥，先关扬声器
  M5Cardputer.Mic.begin();
}

void loop(void) {
  M5Cardputer.update();
  if (M5Cardputer.Mic.isEnabled()) {
    auto data = &rec_data[rec_record_idx * record_length];
    if (M5Cardputer.Mic.record(data, record_length, record_samplerate)) {
      // data[x] >> 6 得波形幅度，可画 writeFastVLine
    }
  }
  if (M5Cardputer.BtnA.wasHold()) {
    // 调噪声滤波等级
    auto cfg = M5Cardputer.Mic.config();
    cfg.noise_filter_level = (cfg.noise_filter_level + 8) & 255;
    M5Cardputer.Mic.config(cfg);
  }
}
```

### API 速查（demo 页验证过的用法）

| API | 用法 | 说明 |
|---|---|---|
| 开关 | `Mic.begin()` / `Mic.end()` / `Mic.isEnabled()` / `Mic.isRecording()` | |
| 录音 | `Mic.record(int16_t* buf, size_t len, uint32_t samplerate)` → bool | 示例用 17000Hz、每块 240 样本 |
| 配置 | `auto cfg = Mic.config(); Mic.config(cfg);` | `cfg.noise_filter_level` 可调（示例步进 8，&255 回绕） |
| 大缓冲 | `heap_caps_malloc(n, MALLOC_CAP_8BIT)` | 环形缓冲分配在 heap，不进栈 |

`Mic_Class` 完整 API 见 `kb/lib-m5unified.md`。

### 官方注意事项
1. **Mic 与 Speaker 互斥**（示例内注释三遍强调）：切换顺序固定为 `Speaker.end() → Mic.begin()`（录音）与 `Mic.end() → Speaker.begin()`（播放）；播放前还要 `while (Mic.isRecording()) delay(1);` 等录音排空。
2. 播放时 `Speaker.playRaw()` 期间要 `do { delay(1); M5Cardputer.update(); } while (Speaker.isPlaying());` 保持 update 心跳。
3. 官方注明：G0(BtnA) 回放最近几秒录音时**回放音量低于实际音量**（`playRaw(..., false, 1, 0)` 单通道参数所致），属已知行为非 bug。
4. 波形显示技巧：用 `Display.startWrite()` + `writeFastVLine()` + `Display.display()` 做高速重绘，配合 prev_y/prev_h 数组擦旧画新。

---

## microSD（SD 卡）

来源：https://docs.m5stack.com/en/arduino/m5cardputer/sdcard

### 用途
microSD 卡读写（目录列举、建删目录、读写/追加/重命名/删文件、IO 测速），适用于 Cardputer 与 Cardputer-Adv。引脚与 FAT32 要求已录于 `kb/lib-m5cardputer.md` §4.9 与 `kb/cardputer-adv.md`，此处保留官方完整初始化模式与注意事项。

### 关键代码模式（初始化）

```cpp
#include <M5Cardputer.h>
#include <SPI.h>
#include <SD.h>

#define SD_SPI_SCK_PIN  40
#define SD_SPI_MISO_PIN 39
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_CS_PIN   12

void setup() {
  M5Cardputer.begin();
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {  // 25MHz
    // 卡初始化失败或未插卡
    while (1);
  }
  uint8_t cardType = SD.cardType();  // CARD_NONE / CARD_MMC / CARD_SD / CARD_SDHC / UNKNOWN
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);  // MB
}
```

文件操作均走 Arduino 内置 `SD` 库标准模式（`fs::FS` 引用参数）：
`fs.open(path[, FILE_WRITE|FILE_APPEND])`、`fs.mkdir` / `fs.rmdir` / `fs.remove` / `fs.rename`、`SD.totalBytes()` / `SD.usedBytes()`。

### API 速查
- SPI 引脚：SCK=G40，MISO=G39，MOSI=G14，CS=G12（与 `kb/cardputer-adv.md` 一致）。
- 官方驱动：Arduino 内置 `SD` 库（文档链接 https://docs.arduino.cc/libraries/sd/ ），非 M5 自研。

### 官方注意事项
1. **必须 FAT32 格式化**的卡；插卡方向：卡触点朝向**与屏幕相反**的方向。
2. 大容量 exFAT 卡需重格为 FAT32 或换 SdFat 库（容量/格式细节见 `kb/cardputer-adv.md` microSD 节）。
3. SPI 总线与 EXT 扩展口复用（G40/G14/G39），并发外设注意片选冲突（见 `kb/cardputer-adv.md` 坑 #7）。
4. 官方日志技巧：`M5Canvas` 设 `setColorDepth(1)` 单色 + `setTextScroll(true)` 做低成本滚动日志屏。

---

## Speaker（扬声器）

来源：https://docs.m5stack.com/en/arduino/m5cardputer/speaker

### 用途
蜂鸣音与 WAV 文件播放，适用于 Cardputer 与 Cardputer-Adv（Adv 的 3.5mm AUX 口同样适用本页 API）。

### 关键代码模式（蜂鸣）

```cpp
#include "M5Cardputer.h"

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);
}

void loop() {
  M5Cardputer.Speaker.tone(7000, 100);  // frequency, duration(ms)
  delay(1000);
  M5Cardputer.Speaker.tone(4000, 20);
  delay(1000);
}
```

WAV 播放：用 **M5Unified** 库示例 `Advanced -> Speaker_SD_wav_file`，需改两处：
- `SDCARD_CSPIN` 改为 `GPIO_NUM_12`；
- `files[]` 数组里的文件名改为 microSD 上的 WAV 路径（前导 `/` 表示卡根目录）。

### API 速查
- `M5Cardputer.Speaker` 为 M5Unified `Speaker_Class`：`tone(freq, duration)` / `playRaw()` / `setVolume()` 等，完整见 `kb/lib-m5unified.md` 与 `kb/lib-m5cardputer.md` §4.4。

### 官方注意事项
1. **3.5mm AUX（仅 Adv）**：插入耳机/外置音箱后，音频输出自动从内置扬声器切到 AUX 通道（即内置功放被禁用，与 `kb/cardputer-adv.md` 坑 #4 一致）。调试"无声"先拔耳机。
2. WAV 示例属于 M5Unified 库而非 M5Cardputer 库，注意去对的示例菜单找。
3. Mic/Speaker 互斥约束见 [Mic](#mic麦克风) 节。
