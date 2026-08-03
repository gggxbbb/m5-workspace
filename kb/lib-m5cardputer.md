# M5Cardputer 库（v1.1.1）

> **核实版本**: 1.1.1，源头 `C:\Users\gameg\Documents\Arduino\libraries\M5Cardputer`，逐文件比对源码。

## 1. 定位与适用设备

M5Cardputer 是 M5Stack Cardputer 系列（初代 Cardputer v1.0/v1.1 + Cardputer-Adv K132-Adv）的统一驱动库。**一台设备、一个库、一个 FQBN**（`m5stack:esp32:m5stack_cardputer`），库在运行时自动识别板型并切换键盘驱动路径。

**何时用这个库而不是 M5Unified 裸用：**
- 需要键盘输入 → 必须用 M5Cardputer（`Keyboard_Class` 及两种 KeyReader 实现仅在 M5Cardputer 库内）。
- 需要 M5Cardputer 专用的 Display/Speaker/Mic/BtnA 快捷引用 → 用 M5Cardputer 更方便。
- 只需屏幕绘制/电源管理/IMU（Cardputer-Adv）→ 可以直接用 M5Unified。

**关键区别 — 初代 vs Adv：**

| 项目 | Cardputer v1.0/v1.1 | Cardputer-Adv (K132-Adv) |
|---|---|---|
| M5Unified board_t | `board_M5Cardputer` | `board_M5CardputerADV` |
| 键盘扫描 | 74HC138 IO 矩阵 (GPIO 直驱) | TCA8418RTWR I2C 键盘芯片 (addr 0x34) |
| 音频 | NS4168 功放 + SPM1423 硅麦 | ES8311 codec + NS4150B 功放 + MEMS 麦 + 3.5mm 耳机口 |
| IMU | 无 | BMI270 (6 轴) |
| 红外 | G44 IR TX | G44 IR TX（同上） |
| 电池 | 120mAh + 1400mAh 双电池 | 单节 1750mAh |

来源：`Keyboard.cpp` 中 `M5.getBoard()` 分支选择。

## 2. 库间关系

### 依赖
- **M5Unified** (≥ 0.2.8)：底层硬件初始化、电源管理、屏幕驱动、Speaker/Mic/IMU/I2C。`M5Cardputer.begin()` 内部调用 `M5.begin()`。
- **M5GFX** (≥ 0.2.10)：显示绘制。`M5Cardputer.Display` 就是 `M5.Display` 的引用。
- **IRremote**：红外发射（M5Cardputer 自身不封装 IR，用户直接调用 IRremote）。
- **LibSSH-ESP32**：SSH 客户端（Advanced 示例依赖）。

### API 重叠区
`M5Cardputer.Display` = `M5.Display`（直接引用，不是拷贝）。同理 `Speaker`、`Mic`、`Power`、`In_I2C`、`Ex_I2C` 全都是 M5Unified 对应成员的引用。

### 重要规则：M5Cardputer 与 M5Unified 二选一 begin
```cpp
// ✅ 正确：用 M5Cardputer.begin()，它内部调用 M5.begin()
auto cfg = M5.config();
M5Cardputer.begin(cfg, true);

// ❌ 错误：不要自己调 M5.begin() 又调 M5Cardputer.begin()
M5.begin();           // 第一次初始化
M5Cardputer.begin();  // 第二次 begin → 重复初始化，Keyboard 可能状态异常
```

来源：`M5Cardputer.cpp` 第 10-18 行。

## 3. 初始化模式

### 3.1 两种 begin 重载

```cpp
// 来自 M5Cardputer.h 第 14-15 行
void begin(bool enableKeyboard = true);
void begin(m5::M5Unified::config_t cfg, bool enableKeyboard = true);
```

- `cfg`：M5Unified 的配置结构体，通过 `M5.config()` 获取默认值后修改。
- `enableKeyboard`：选 `true` 才开始键盘扫描（省电/不需要键盘时选 `false`）。

### 3.2 begin 内部流程（源码核实）

```
M5Cardputer::begin(cfg, enableKeyboard)
  ├── M5.begin(cfg)                    // 初始化 M5Unified（屏幕/I2C/音频/电源）
  └── if (enableKeyboard)
        └── Keyboard.begin()           // 见下
              ├── board = M5.getBoard()
              ├── if board == board_M5Cardputer    → new IOMatrixKeyboardReader()
              ├── if board == board_M5CardputerADV → new TCA8418KeyboardReader()
              └── reader->begin()      // 初始化对应硬件
```

**IOMatrixKeyboardReader::begin()** — GPIO 直驱模式：
- output_list = {8, 9, 11} 设为 OUTPUT
- input_list = {13, 15, 3, 4, 5, 6, 7} 设为 INPUT_PULLUP
- 使用 74HC138 做行扫描

**TCA8418KeyboardReader::begin()** — I2C 键盘芯片模式：
- I2C 地址 0x34，总线为 `m5::In_I2C` (G8=SDA, G9=SCL)
- 配置 7 rows × 8 columns 矩阵
- G11 设为 INPUT 并挂 `attachInterruptArg(CHANGE)` ISR

来源：`IOMatrix.cpp`、`TCA8418.cpp`。

### 3.3 M5.config() 常用配置字段

```cpp
auto cfg = M5.config();
// cfg.serial_baudrate = 115200;
// cfg.output_power   = true;    // Grove/EXT 口供电
// cfg.external_speaker = false; // 使用内置扬声器（默认）
M5Cardputer.begin(cfg, true);
```

注意：`m5::M5Unified::config_t` 字段定义在 M5Unified 库中，不在此库中。

## 4. 核心 API 速查表

### 4.1 全局对象

```cpp
// M5Cardputer.h 第 16-25 行 — 全部是引用
m5::M5_CARDPUTER M5Cardputer;     // 全局单例
M5Cardputer.Display               // M5GFX& = M5.Display — ST7789V2 240x135
M5Cardputer.Lcd                   // = Display 的别名（保持旧代码兼容）
M5Cardputer.Power                 // Power_Class& = M5.Power
M5Cardputer.Speaker               // Speaker_Class& = M5.Speaker
M5Cardputer.Mic                   // Mic_Class& = M5.Mic
M5Cardputer.BtnA                  // Button_Class& = M5.getButton(0) — G0 按键
M5Cardputer.In_I2C                // I2C_Class& = m5::In_I2C — 内部 I2C (G8/G9)
M5Cardputer.Ex_I2C                // I2C_Class& = m5::Ex_I2C — Grove 口 I2C (G1/G2)
```

**BtnA 说明**：`M5.getButton(0)` 返回的是 G0 引脚按键。Cardputer 上 G0 在 Stamp-S3A 模组上，通常用做 Boot 键（按住插电进下载模式），但在用户程序中可作为普通按钮使用。Adv 上 G38 需先拉高才能用 RGB LED。

### 4.2 M5Cardputer.update()

```cpp
// M5Cardputer.cpp 第 28-33 行
void M5_CARDPUTER::update(void)
{
    M5.update();                     // M5Unified 的 update（按键/电源/IMU）
    if (_enableKeyboard) {
        Keyboard.updateKeyList();    // 读取硬件原始数据 → _key_list
        Keyboard.updateKeysState();  // 解析修饰键 + 生成 word/hid_keys
    }
}
```

**必须在 loop() 中持续调用 `M5Cardputer.update()`**，频率越高越好，阻塞会漏键。

### 4.3 键盘 API — Keyboard_Class

全部 API 来源文件：`utility/Keyboard/Keyboard.h`、`Keyboard.cpp`。

#### 4.3.1 轮询状态

```cpp
// Keyboard.h 第 101-102 行, Keyboard.cpp 第 63-71 行
uint8_t Keyboard_Class::isPressed();   // 当前按下的键数 (keyList().size())
bool Keyboard_Class::isChange();       // 按键数是否变化（与上一次 update 比较）
```

#### 4.3.2 单键查询

```cpp
// Keyboard.h 第 104 行, Keyboard.cpp 第 73-81 行
bool Keyboard_Class::isKeyPressed(char c);  // 检查字符 c 是否在按下键中
                                            // 受 shift/ctrl/caps 影响（调用 getKey）
```

#### 4.3.3 按坐标取值

```cpp
// Keyboard.h 第 95 行, Keyboard.cpp 第 46-56 行
uint8_t Keyboard_Class::getKey(Point2D_t keyCoor);
// 返回坐标对应的 ASCII 字符，会考虑 shift/ctrl/caps 状态
// 坐标范围: x=0..13, y=0..3（4 行 × 14 列）
```

#### 4.3.4 KeysState 结构体 — 核心输出

```cpp
// Keyboard.h 第 74-95 行
struct Keyboard_Class::KeysState {
    bool tab          = false;    // Tab 键是否按下
    bool fn           = false;    // Fn 键（特殊修饰，非标准 USB HID）
    bool shift        = false;    // Shift
    bool ctrl         = false;    // Ctrl (映射到 USB HID LEFT CTRL 0xE0)
    bool opt          = false;    // Opt 键（Cardputer 特有，非标准 HID）
    bool alt          = false;    // Alt (映射到 USB HID LEFT ALT 0xE2)
    bool del          = false;    // Backspace 按下
    bool enter        = false;    // Enter 按下
    bool space        = false;    // 空格按下
    uint8_t modifiers = 0;        // USB HID 修饰键位掩码
    std::vector<char> word;       // <—— 打印字符列表（考虑了 shift/ctrl/caps）
    std::vector<uint8_t> hid_keys;     // USB HID 键码（printable + space/enter/del）
    std::vector<uint8_t> modifier_keys; // 修饰键的原始键码 (0x80/0x81/0x82)
    void reset();                 // 清空所有字段
};
// 获取当前状态:
Keyboard_Class::KeysState& Keyboard_Class::keysState();
```

**word vs hid_keys 的语义差异**：
- `word`：文本字符，已经应用了 shift/ctrl/caps（例如按 Shift+A → `word` 含 `'A'`）。用于屏幕回显。
- `hid_keys`：USB HID usage ID（例如 `'a'` → 0x04，`'A'` → 0x04 | SHIFT）。含 Backspace(0x2A)/Enter(0x28)/Tab(0x2B)。用于 `USBHIDKeyboard.sendReport()`。
- `modifiers`：位掩码（bit 0=ctrl, bit 1=shift, bit 2=alt...），直接填入 `KeyReport.modifiers`。

#### 4.3.5 键值映射表

```cpp
// Keyboard.h 第 21-63 行 — 4×14 二维数组，value_first = 小写, value_second = 大写
const KeyValue_t _key_value_map[4][14] = {
  // Row 0: ` 1 2 3 4 5 6 7 8 9 0 - = BACKSPACE
  // Row 1: TAB q w e r t y u i o p [ ] \
  // Row 2: FN SHIFT a s d f g h j k l ; ' ENTER
  // Row 3: CTRL OPT ALT z x c v b n m , . / SPACE
};
```

#### 4.3.6 Caps Lock

```cpp
// Keyboard.h 第 114-120 行
bool Keyboard_Class::capslocked();            // 查询大写锁定状态
void Keyboard_Class::setCapsLocked(bool);     // 设置大写锁定（库只存储不驱动 LED）
```

**注意**：Fn 和 Opt 键不是标准 USB HID 修饰键。Fn 在 `updateKeysState()` 中只设置 `_keys_state_buffer.fn = true`，不加入 `modifiers` 或 `hid_keys`。如果要做 Fn+数字 = F1-F12 的功能，需要自己读取 `status.fn` 后映射。

### 4.4 扬声器 API — Speaker_Class（ES8311）

```cpp
// 引用自 M5Unified，以下为 Cardputer 示例中验证过的 API：

M5Cardputer.Speaker.tone(frequency, duration_ms);  // 播放单音
M5Cardputer.Speaker.setVolume(0-255);              // 音量
M5Cardputer.Speaker.begin();                       // 初始化扬声器
M5Cardputer.Speaker.end();                         // 关闭扬声器（与 Mic 互斥）
M5Cardputer.Speaker.isEnabled();                   // 是否已启用
M5Cardputer.Speaker.isPlaying();                   // 是否正在播放
M5Cardputer.Speaker.playRaw(buf, len, rate, ...); // 播放原始 PCM
```

### 4.5 麦克风 API — Mic_Class

```cpp
// 引用自 M5Unified
M5Cardputer.Mic.begin();                           // 初始化麦克风
M5Cardputer.Mic.end();                             // 关闭麦克风
M5Cardputer.Mic.isEnabled();                       // 是否已启用
M5Cardputer.Mic.isRecording();                     // 是否正在录音
M5Cardputer.Mic.record(buf, len, samplerate);      // 录制 PCM 到缓冲区
M5Cardputer.Mic.config();                          // 获取/设置 Mic 配置（含 noise_filter_level）
```

**关键约束**：扬声器和麦克风不能同时使用。mic 示例的模式切换：
```
录音模式: Speaker.end() → Mic.begin()
播放模式: Mic.end()    → Speaker.begin()
```

### 4.6 按钮 API — Button_Class（BtnA = G0）

```cpp
// 引用自 M5Unified
M5Cardputer.BtnA.wasPressed();     // 从"未按下"变为"按下"的边沿
M5Cardputer.BtnA.wasReleased();    // 从"按下"变为"未按下"的边沿
M5Cardputer.BtnA.wasClicked();     // 短按（按下+释放）
M5Cardputer.BtnA.wasHold();        // 长按
M5Cardputer.BtnA.isPressed();      // 当前是否按下
```

### 4.7 屏幕 API — M5GFX

`M5Cardputer.Display` 即 LovyanGFX 实例。Cardputer 屏幕：ST7789V2，240×135（横屏模式 `setRotation(1)` 为 240×135）。

```cpp
M5Cardputer.Display.setRotation(1);        // 横屏
M5Cardputer.Display.width();               // 240
M5Cardputer.Display.height();              // 135
// 所有 LovyanGFX 绘制 API 均可用：fillRect, drawString, drawRect, pushSprite, ...
```

### 4.8 红外发射

M5Cardputer 库不封装 IR。用户直接调用 IRremote：
```cpp
#include <IRremote.h>
// IR TX 引脚: G44
IrSender.sendNEC(address, command, repeats);
```

### 4.9 microSD 卡

SD 卡通过 SPI 连接（与屏幕共用 SPI 总线，CS 独立）：

```cpp
// sdcard.ino 示例（来源核实）
#include <SPI.h>
#include <SD.h>

#define SD_SPI_SCK_PIN  40    // G40
#define SD_SPI_MISO_PIN 39    // G39
#define SD_SPI_MOSI_PIN 14    // G14
#define SD_SPI_CS_PIN   12    // G12

SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
SD.begin(SD_SPI_CS_PIN, SPI, 25000000);  // 25MHz
```

## 5. 易错点与反模式

### 5.1 键盘自动切换机制

库在 `Keyboard::begin()` 中通过 `M5.getBoard()` 判断板型。M5Unified 通过 I2C 探测 TCA8418（地址 0x34）是否存在来区分：
- 探测成功 → `board_M5CardputerADV` → `TCA8418KeyboardReader`
- 探测失败 → `board_M5Cardputer` → `IOMatrixKeyboardReader`

**反模式**：不要手动判断板型再创建 Reader，统一用 `M5Cardputer.begin()` 即可。

### 5.2 M5Cardputer vs M5Unified 二选一

**绝对不要混用 `begin()`**：
```cpp
// ❌ 错误
M5.begin();
M5Cardputer.begin();  // 内部又调了一次 M5.begin()

// ✅ 正确
auto cfg = M5.config();
M5Cardputer.begin(cfg, true);
```

### 5.3 键盘 polling 频率

- `M5Cardputer.update()` 必须在 `loop()` 中无阻塞地持续调用。
- **初代 74HC138**：每次 `update()` 扫描全部 8 行（每行读 7 列），速度较快但多键同按时可能漏读（multiPress 示例注释 "max press 3 button at the same time"）。
- **Adv TCA8418**：依赖硬件中断。按键事件进入芯片内部 FIFO，`update()` 在 ISR 触发后每次只读取一个事件。如果在两次 `update()` 之间 FIFO 积累多个事件，会分批处理（`_isr_flag` 只有完全清空 INT_STAT 后才清）。

### 5.4 TCA8418 ISR 注意事项

`TCA8418KeyboardReader` 使用 `attachInterruptArg` 在 G11 上挂 `CHANGE` 中断。ISR 发送信号后 `update()` 读取并清除事件。如果用户同时使用 G11 做其他用途，会破坏键盘读取。

### 5.5 3.5mm 耳机检测（Adv）

Cardputer-Adv 有 3.5mm 耳机口，插入时硬件自动断开扬声器功放（NS4150B），没有软件检测接口。如果播放音频无声，**先检查耳机口是否插着东西**。

### 5.6 microSD CS (G12) 与屏幕 SPI 冲突

microSD 与屏幕共用 SPI 总线（MOSI=G14, SCK=G40, MISO=G39），仅 CS 不同。同时使用时：
- 屏幕 CS：内部由 M5GFX 管理
- microSD CS：G12，需显式 `SD.begin(G12, SPI, 25000000)`
- 每次 SD 操作后确保 CS 拉高，否则屏幕可能花屏

### 5.7 Cardputer-Adv 无 PSRAM

ESP32-S3FN8 封装无 PSRAM（与 N8R2/N8R8 不同）。大 buffer 场景注意：
- 录音缓冲区不能太大（mic 示例用 `heap_caps_malloc(..., MALLOC_CAP_8BIT)`）
- 大帧缓冲区/canvas sprite 受限

### 5.8 Fn/Opt 键不是标准 USB HID

Fn (0xFF) 和 Opt (0x00) 是 Cardputer 硬件特有键，不在 USB HID 标准中。`KeysState` 的 `hid_keys` 和 `modifiers` 不含它们。如果你要做 Fn+F1=亮度调节之类的映射，需要自己读 `status.fn`。

### 5.9 键盘坐标与行列对应

4 行 × 14 列布局，坐标系在 `IOMatrixKeyboardReader::update()` 中做了 Y 轴翻转（`coor.y = -coor.y + 3`）以匹配物理排列。不要直接操作 `keyList()` 的坐标来做映射——用 `getKey()` 或 `getKeyValue()`。

### 5.10 display 示例中的 canvas 用法

```cpp
M5Canvas canvas(&M5Cardputer.Display);
canvas.setColorDepth(1);  // 单色节省内存
canvas.createSprite(w, h);
canvas.pushSprite(x, y);  // 推到屏幕
```

### 5.11 编译要求（来自 library.properties）

```properties
depends=M5Unified, M5GFX, IRremote, LibSSH-ESP32
```

本机环境：M5Unified 0.2.19、M5GFX 0.2.26。

## 6. 版本兼容

| 版本 | 本地 | 说明 |
|---|---|---|
| M5Cardputer | **1.1.1** | 已核实。加入了 TCA8418 支持（Cardputer-Adv）。 |
| M5Unified | 0.2.19 | ≥ 0.2.8 即满足。 |
| M5GFX | 0.2.26 | ≥ 0.2.10 即满足。 |

**重要变更**：
- v1.1.0+：新增 `TCA8418KeyboardReader` 和 `Adafruit_TCA8418` 驱动，支持 Cardputer-Adv。
- v1.1.0 前：仅支持初代 74HC138 键盘。

## 7. 最小可编译示例 — 键盘输入回显到屏幕

```cpp
/**
 * @brief M5Cardputer 键盘输入回显示例
 * 与库内 examples/Basic/keyboard/inputText 核对一致的模式
 */
#include "M5Cardputer.h"

String inputBuffer = "> ";

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);            // enableKeyboard = true

    M5Cardputer.Display.setRotation(1);       // 横屏
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(GREEN);
    M5Cardputer.Display.drawString("M5Cardputer Ready", 4, 4);
    M5Cardputer.Display.drawString(inputBuffer, 4, 30);
}

void loop() {
    M5Cardputer.update();                    // 必须：刷新键盘状态

    if (M5Cardputer.Keyboard.isChange()) {   // 键盘状态有变化
        if (M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState state =
                M5Cardputer.Keyboard.keysState();

            // 拼接普通字符
            for (auto ch : state.word) {
                inputBuffer += ch;
            }

            // Backspace
            if (state.del && inputBuffer.length() > 2) {
                inputBuffer.remove(inputBuffer.length() - 1);
            }

            // Enter：清除输入行
            if (state.enter) {
                inputBuffer = "> ";
            }

            // 刷新显示
            M5Cardputer.Display.fillRect(
                0, 30, M5Cardputer.Display.width(), 20, BLACK);
            M5Cardputer.Display.drawString(inputBuffer, 4, 30);
        }
    }
}
```

**兼容性**：此代码在初代 Cardputer 和 Cardputer-Adv 上均可运行，库自动处理键盘驱动差异。

## 8. 参考链接

- 库 GitHub：https://github.com/m5stack/M5Cardputer
- Arduino 快速上手：https://docs.m5stack.com/en/arduino/m5cardputer/program
- 键盘 API 文档：https://docs.m5stack.com/en/arduino/m5cardputer/keyboard
- 显示 API 文档：https://docs.m5stack.com/en/arduino/m5cardputer/display
- microSD API 文档：https://docs.m5stack.com/en/arduino/m5cardputer/sdcard
- Cardputer-Adv 产品页：https://docs.m5stack.com/en/core/Cardputer-Adv
- 本仓库设备 KB：`kb/cardputer-adv.md`、`kb/m5stick-s3.md`
