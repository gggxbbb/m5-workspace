# M5GFX — M5Stack 统一显示驱动库

> **本地版本**: 0.2.26（核实来源：`<libraries>\M5GFX`）
> 本地 M5GFX.cpp 共 3484 行，涵盖 ESP32/ESP32-S3/ESP32-P4/SDL 多平台自动识别。

---

## 1. 定位与适用设备

M5GFX 是 M5Stack 生态中**唯一的底层显示驱动库**。所有 M5Stack 设备（Core/Core2/CoreS3/StickC/StickCPlus/StickCPlus2/StickS3/Cardputer/Cardputer-Adv/ATOM/Dial/Paper...）的屏幕绘制都通过 M5GFX 完成。它基于 **LovyanGFX** 的 fork，在 LovyanGFX 基础上增加了 M5Stack 设备的**自动板型识别（autodetect）**、板级电源/背光管理、以及 M5Canvas 等封装。

**何时用 M5GFX 而不是别的库**：
- 任何 M5Stack 设备的屏幕绘制 → M5GFX（或通过 M5Unified 的 `M5.Display`）
- 需要 sprite 离屏渲染 → M5GFX 的 `M5Canvas`（底层是 `LGFX_Sprite`）
- 需要 JPEG/PNG/BMP/QOI 图片解码 → M5GFX 内置解码器
- 需要 VLW/efont/U8g2 字体 → M5GFX 字体系统

**不要做的事**：
- 不要直接用 LovyanGFX 替代 M5GFX：LovyanGFX 没有 M5Stack 板型自动识别，引脚/背光配置需要手动完成
- 不要用 TFT_eSPI / Adafruit_GFX 替代：它们在 M5Stack 设备上不与 PMU/电源管理配合

---

## 2. 库间关系

### 依赖链（从源码 `#include` 核实）

```
M5GFX (src/M5GFX.h, src/M5GFX.cpp)
  └── LovyanGFX (src/lgfx/v1/*)
        ├── LGFX_Device  (src/lgfx/v1/LGFXBase.hpp:1418)  ← M5GFX 的父类
        │     └── LovyanGFX (LGFXBase.hpp:1406)
        │           └── LGFXBase (LGFXBase.hpp:78)  ← 继承 Arduino Print
        ├── LGFX_Sprite   (src/lgfx/v1/LGFX_Sprite.hpp)  ← M5Canvas 的父类
        ├── LGFX_Button   (src/lgfx/v1/LGFX_Button.hpp)
        └── Panel_Device / Panel_ST7789 / Panel_ILI9342 / ... (src/lgfx/v1/panel/)
```

### 被谁依赖
- **M5Unified** — `M5.Display` 字段类型就是 `M5GFX`（`M5Unified.hpp:221`）。`M5.begin()` 内部调用 `Display.init()` 触发 autodetect，之后所有绘制走 `M5.Display.xxx()`。
- **M5Cardputer** — 依赖 M5GFX 做屏幕显示。

### API 重叠区
| 场景 | 用这个 | 不要用那个 |
|---|---|---|
| M5Stack 项目（有 M5Unified） | `M5.Display.drawPixel(...)` | `M5GFX display; display.begin();` |
| 纯 M5GFX 独立使用（无 M5Unified） | `M5GFX display; display.begin();` | — |
| 创建 Sprite | `M5Canvas sprite(&M5.Display);` | 直接 `new LGFX_Sprite` |
| 加载 VLW 字体 | `M5.Display.loadFont(path)` | 直接用 LovyanGFX API |

---

## 3. 初始化模式

### 3.1 通过 M5Unified（推荐）

```cpp
#include <M5Unified.h>

void setup() {
    auto cfg = M5.config();
    // cfg.external_display = false;  // 禁用外接显示器自动检测
    M5.begin(cfg);

    // M5.Display 此时已经过 autodetect，可直接绘制
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("Hello");
}
```

**关键事实**（来自 `M5Unified.hpp:340-389` 源码）：
- `M5.begin()` 先调用 `Display.init()` 或 `Display.init_without_reset(false)`
- `init()` → `init_impl(true, true)` 含硬件 reset + 清屏
- `init_without_reset(false)` → `init_impl(false, false)` 跳过硬件 reset
- autodetect 结果缓存在 NVS，第二次启动直接读取，大幅加速
- 初始化后 `addDisplay(Display)` 把 Display 注册到 M5Unified 的内部 display 列表

### 3.2 独立使用 M5GFX（无 M5Unified）

```cpp
#include <M5GFX.h>

M5GFX display;

void setup() {
    display.begin();           // = init() = init_impl(true, true)
    // 或 display.init_without_reset(false);  // 跳过硬件 reset

    display.fillScreen(TFT_BLACK);
}
```

### 3.3 板型自动识别机制（autodetect）

`M5GFX::init_impl()`（`src/M5GFX.cpp:816-933`）的工作流程：

1. 检查 `getBoard()` 是否已设置 → 已设置直接返回（防重复初始化）
2. 从 NVS 读取上次缓存的 board 类型
3. 若无 NVS 缓存，检查编译宏（`M5GFX_BOARD` 或 Arduino 板型宏如 `ARDUINO_M5STACK_CORE_ESP32`）
4. **最多重试 4 次**调用 `autodetect(use_reset, board)`
5. autodetect 成功后把 board 类型写入 NVS
6. 调用 `LGFX_Device::init_impl(false, use_clear)` 完成底层面板初始化

**autodetect 对不同设备的具体行为**（均来自 `src/M5GFX.cpp`）：

#### StickS3（`board_M5StickS3 = 26`，`M5GFX.cpp:2575-2650`）
- 检测 G47/G48（I2C SDA/SCL）是否有上拉
- 读 M5PM1 设备 ID（I2C 0x6E reg 0x00）验证
- 配置 `Panel_ST7789`：
  - SPI: MOSI=G39, SCLK=G40, DC=G45, CS=G41, RST=G21
  - `panel_width=135, panel_height=240`
  - `offset_x=52, offset_y=40, offset_rotation=0`
  - `invert=true, readable=true, bus_shared=false`
  - SPI3_HOST, freq_write=40MHz, freq_read=16MHz
  - 背光：PWM G38, ch7, freq=256, offset=16

#### Cardputer / Cardputer-Adv（`M5GFX.cpp:2292-2410`）
- 检测 SPI ST7789 面板 ID（0x81 或 0x85）
- **区分 Cardputer vs Cardputer-Adv**：检查 G8/G9（I2C）是否有上拉（`result & 0x0C == 0x0C`）→ 有上拉 = Cardputer-Adv（有 TCA8418）
- 配置 `Panel_ST7789`：
  - SPI: MOSI=G35, SCLK=G36, DC=G34, CS=G37, RST=G33
  - `panel_width=135, panel_height=240`
  - `offset_x=52, offset_y=40, offset_rotation=0`
  - `invert=true, readable=true`
  - `rotation=1`（横屏模式，240×135）
  - SPI3_HOST, freq_write=40MHz, freq_read=16MHz
  - 背光：PWM G38, ch7, freq=256, offset=16

### 3.4 M5Canvas（Sprite）初始化

```cpp
#include <M5GFX.h>
// 或 #include <M5Unified.h>

// 方式1：父 Display 构造（PSRAM 优先）
M5Canvas sprite(&M5.Display);

// 方式2：默认构造（无 PSRAM）
M5Canvas sprite;

// 方式3：显式控制 PSRAM
M5Canvas sprite(&M5.Display);
sprite.setPsram(false);   // 强制用内部 RAM（Cardputer-Adv 无 PSRAM 时必须）

// 创建 sprite
sprite.createSprite(135, 240);   // 返回 void* buffer
// 或
sprite.setColorDepth(8);          // 8bit 调色板模式，省内存
sprite.createSprite(135, 240);    // 以新色深创建

// 释放
sprite.deleteSprite();
```

**关键**：`M5Canvas(LovyanGFX* parent)` 构造函数设置 `_psram = true`（`M5GFX.h:236-237`）。在 **Cardputer-Adv（无 PSRAM）** 上必须显式 `setPsram(false)` 否则 `createSprite` 会从 PSRAM 分配失败。

---

## 4. 核心 API 速查表

> 所有 API 均为 `M5GFX`（即 `LGFX_Device`）或其父类方法。`M5.Display` 等价于 `M5GFX` 实例。

### 4.1 初始化和显示控制

| API | 签名 | 说明 | 来源 |
|---|---|---|---|
| `begin` | `bool begin(void)` | = `init()` = `init_impl(true, true)` | `LGFXBase.hpp:1425` |
| `init` | `bool init(void)` | 同 begin | `LGFXBase.hpp:1425` |
| `init_without_reset` | `bool init_without_reset(bool clear=false)` | 跳过硬件 reset | `LGFXBase.hpp:1426` |
| `getBoard` | `board_t getBoard(void) const` | 返回 autodetect 结果，如 `board_M5StickS3` | `LGFXBase.hpp:1427` |
| `setBrightness` | `void setBrightness(uint8_t brightness)` | 0-255 背光亮度 | `LGFXBase.hpp:1460` |
| `getBrightness` | `uint8_t getBrightness(void) const` | 获取当前亮度，默认 127 | `LGFXBase.hpp:1461` |
| `sleep` | `void sleep(void)` | 背光置 0 + 面板 sleep | `LGFXBase.hpp:1436` |
| `wakeup` | `void wakeup(void)` | 面板唤醒 + 恢复亮度 | `LGFXBase.hpp:1437` |
| `powerSaveOn/Off` | `void powerSaveOn(void)` | 面板省电模式 | `LGFXBase.hpp:1439-1440` |
| `setColorDepth` | `void setColorDepth(int bits)` | 设置色深，支持 1/2/4/8/16/24 bit | `LGFXBase.hpp:638-639` |
| `getColorDepth` | `color_depth_t getColorDepth(void) const` | 获取当前色深 | `LGFXBase.hpp:138` |

### 4.2 基本绘制

| API | 签名 | 说明 | 来源 |
|---|---|---|---|
| `fillScreen` | `void fillScreen(T color)` | 清屏填充 | `LGFXBase.hpp:311` |
| `clear` | `void clear(void)` | = `clearDisplay()` | `LGFXBase.hpp:313-314` |
| `clearDisplay` | `void clearDisplay(T color)` | 用背景色清屏 | `LGFXBase.hpp:316` |
| `drawPixel` | `void drawPixel(int32_t x, int32_t y, T color)` | 画像素点 | `LGFXBase.hpp:162-163` |
| `fillRect` | `void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, T color)` | 填充矩形 | `LGFXBase.hpp:198-199` |
| `drawRect` | `void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, T color)` | 画空心矩形 | `LGFXBase.hpp:209-210` |
| `drawCircle` | `void drawCircle(int32_t x, int32_t y, int32_t r, T color)` | 画空心圆 | `LGFXBase.hpp:224` |
| `fillCircle` | `void fillCircle(int32_t x, int32_t y, int32_t r, T color)` | 填充圆 | `LGFXBase.hpp:226` |
| `drawLine` | `void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, T color)` | 画直线 | `LGFXBase.hpp:232` |
| `drawRoundRect` | `void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, T color)` | 画圆角矩形 | `LGFXBase.hpp:218` |
| `fillRoundRect` | `void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, T color)` | 填充圆角矩形 | `LGFXBase.hpp:220` |
| `drawEllipse` | `void drawEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, T color)` | 画椭圆 | `LGFXBase.hpp:228` |
| `fillEllipse` | `void fillEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, T color)` | 填充椭圆 | `LGFXBase.hpp:230` |
| `drawTriangle` | `void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, T color)` | 画三角形 | `LGFXBase.hpp:234` |
| `fillTriangle` | `void fillTriangle(...)` | 填充三角形 | `LGFXBase.hpp:236` |
| `drawBezier` | `void drawBezier(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, T color)` | 画贝塞尔曲线（3 点） | `LGFXBase.hpp:238` |
| `drawBezier` | `void drawBezier(int32_t x0,..., int32_t x3, int32_t y3, T color)` | 画贝塞尔曲线（4 点） | `LGFXBase.hpp:240-241` |
| `drawArc` | `void drawArc(int32_t x, int32_t y, int32_t r0, int32_t r1, float angle0, float angle1)` | 画弧线 | `LGFXBase.hpp:248` |
| `fillArc` | `void fillArc(...)` | 填充扇形 | `LGFXBase.hpp:250` |

### 4.3 颜色与配置

| API | 签名 | 说明 | 来源 |
|---|---|---|---|
| `setColor` | `void setColor(uint8_t r, uint8_t g, uint8_t b)` | 设置绘图颜色（RGB888 → 内部转换） | `LGFXBase.hpp:115` |
| `setColor` | `void setColor(T color)` | T=uint16_t(RGB565), uint32_t 等 | `LGFXBase.hpp:116` |
| `getRawColor` | `uint32_t getRawColor(void) const` | 获取当前原始颜色值 | `LGFXBase.hpp:117` |
| `setBaseColor` | `void setBaseColor(T c)` | 设置背景色（用于 clear） | `LGFXBase.hpp:118` |
| `getBaseColor` | `uint32_t getBaseColor(void) const` | 获取背景色 | `LGFXBase.hpp:119` |
| `color565` | `static uint16_t color565(uint8_t r, uint8_t g, uint8_t b)` | RGB → RGB565 | `LGFXBase.hpp:101` |
| `color888` | `static uint32_t color888(uint8_t r, uint8_t g, uint8_t b)` | RGB → RGB888 | `LGFXBase.hpp:103` |

**内置颜色常量**（`M5GFX.h:75-108`，命名空间 `m5gfx::ili9341_colors`）：

| 常量 | 值 | 常量 | 值 |
|---|---|---|---|
| `TFT_BLACK` / `BLACK` | 0x0000 | `TFT_WHITE` / `WHITE` | 0xFFFF |
| `TFT_RED` / `RED` | 0xF800 | `TFT_GREEN` / `GREEN` | 0x07E0 |
| `TFT_BLUE` / `BLUE` | 0x001F | `TFT_YELLOW` / `YELLOW` | 0xFFE0 |
| `TFT_CYAN` / `CYAN` | 0x07FF | `TFT_MAGENTA` / `MAGENTA` | 0xF81F |
| `TFT_ORANGE` / `ORANGE` | 0xFDA0 | `TFT_PINK` / `PINK` | 0xFE19 |
| `TFT_PURPLE` / `PURPLE` | 0x780F | `TFT_NAVY` / `NAVY` | 0x000F |
| `TFT_DARKGREEN` | 0x03E0 | `TFT_DARKCYAN` | 0x03EF |
| `TFT_MAROON` | 0x7800 | `TFT_OLIVE` | 0x7BE0 |
| `TFT_LIGHTGREY` | 0xD69A | `TFT_DARKGREY` | 0x7BEF |
| `TFT_GREENYELLOW` | 0xB7E0 | `TFT_BROWN` | 0x9A60 |
| `TFT_GOLD` | 0xFEA0 | `TFT_SILVER` | 0xC618 |
| `TFT_SKYBLUE` | 0x867D | `TFT_VIOLET` | 0x915C |

> 注：`M5GFX.h` 末尾有 `using namespace m5gfx::ili9341_colors;`，所以代码中可直接用 `TFT_BLACK` 等。

### 4.4 文字与字体

| API | 签名 | 说明 | 来源 |
|---|---|---|---|
| `setFont` | `void setFont(const IFont* font)` | 设置字体 | `LGFXBase.hpp:815` |
| `getFont` | `const IFont* getFont(void) const` | 获取当前字体 | `LGFXBase.hpp:813` |
| `setTextSize` | `void setTextSize(float sx, float sy)` | 设置文字缩放（默认 1.0） | `LGFXBase.hpp:672-673` |
| `setTextColor` | `void setTextColor(T fg)` | 设置文字前景色 | `LGFXBase.hpp:686` |
| `setTextColor` | `void setTextColor(T fg, T bg)` | 设置文字前景+背景色 | `LGFXBase.hpp:688` |
| `setTextDatum` | `void setTextDatum(textdatum_t datum)` | 文字对齐基准点 | `LGFXBase.hpp:680` |
| `setTextWrap` | `void setTextWrap(bool wrapX, bool wrapY=false)` | 文字自动换行 | `LGFXBase.hpp:683` |
| `setTextScroll` | `void setTextScroll(bool scroll)` | 文字滚动（print 溢出时） | `LGFXBase.hpp:684` |
| `setCursor` | `void setCursor(int32_t x, int32_t y)` | 设置 print 光标位置 | `LGFXBase.hpp:667` |
| `getCursorX/Y` | `int32_t getCursorX/Y(void) const` | 获取光标位置 | `LGFXBase.hpp:668-669` |
| `drawString` | `size_t drawString(const char* s, int32_t x, int32_t y)` | 画字符串 | `LGFXBase.hpp:722` |
| `drawString` | `size_t drawString(const char* s, int32_t x, int32_t y, const IFont* f)` | 指定字体画字符串 | `LGFXBase.hpp:723` |
| `drawNumber` | `size_t drawNumber(long n, int32_t x, int32_t y)` | 画数字 | `LGFXBase.hpp:727` |
| `drawFloat` | `size_t drawFloat(float f, uint8_t dp, int32_t x, int32_t y)` | 画浮点数 | `LGFXBase.hpp:731` |
| `drawCentreString` | `size_t drawCentreString(const char* s, int32_t x, int32_t y)` | 居中画字符串 | `LGFXBase.hpp:737` |
| `drawRightString` | `size_t drawRightString(const char* s, int32_t x, int32_t y)` | 右对齐画字符串 | `LGFXBase.hpp:738` |
| `printf` | `size_t printf(const char* format, ...)` | printf 风格（ESP32 平台可用） | `LGFXBase.hpp:900-903` |
| `print/println` | `size_t print(...)` | 继承自 Arduino Print | `LGFXBase.hpp:865+` |
| `loadFont` | `bool loadFont(const char* path)` | 从文件系统加载 VLW 字体 | `LGFXBase.hpp:828` |
| `loadFont` | `bool loadFont(const uint8_t* array)` | 从内存数组加载 VLW 字体 | `LGFXBase.hpp:826` |
| `unloadFont` | `void unloadFont(void)` | 卸载运行时字体 | `LGFXBase.hpp:844` |
| `fontHeight` | `int32_t fontHeight(void) const` | 当前字体高度 | `LGFXBase.hpp:714` |
| `textWidth` | `int32_t textWidth(const char* s)` | 字符串像素宽度 | `LGFXBase.hpp:716` |
| `qrcode` | `void qrcode(const char* s, int32_t x, int32_t y, int32_t w, uint8_t version=1)` | 绘制二维码 | `LGFXBase.hpp:914` |

**内置字体**（`src/lgfx/v1/lgfx_fonts.hpp:345-562`）：

| 字体 | 类型 | 说明 |
|---|---|---|
| `fonts::Font0` | GLCD 6×8 | 默认字体，仅 ASCII |
| `fonts::Font2` | BMP 16px | ASCII |
| `fonts::Font4` | RLE 26px | ASCII |
| `fonts::Font6` | RLE 36px | ASCII |
| `fonts::Font7` | RLE 48px（7seg 风格） | ASCII |
| `fonts::Font8` | RLE 75px（7seg 风格） | ASCII |
| `fonts::Font8x8C64` | GLCD 8×8 | C64 风格 |
| `fonts::AsciiFont8x16` | FixedBMP 8×16 | ASCII |
| `fonts::AsciiFont24x48` | FixedBMP 24×48 | ASCII |
| `fonts::DejaVu9/12/18/24/40/56/72` | GFXfont | DejaVu Sans |
| `fonts::FreeMono9pt7b` 等 | GFXfont | FreeMono 系列 |
| `fonts::FreeSans9pt7b` 等 | GFXfont | FreeSans 系列 |
| `fonts::FreeSerif9pt7b` 等 | GFXfont | FreeSerif 系列 |
| `fonts::TomThumb` | GFXfont | 超小字体 |
| `fonts::Orbitron_Light_24/32` | GFXfont | 科技感字体 |
| `fonts::Roboto_Thin_24` | GFXfont | Roboto Thin |
| `fonts::lgfxJapanMincho_8~40` | U8g2font | 日文明朝体 |
| `fonts::lgfxJapanGothic_8~40` | U8g2font | 日文哥特体 |
| `fonts::efontCN_10~24` | U8g2font | **简体中文**（10/12/14/16/24 号） |
| `fonts::efontJA_10~24` | U8g2font | 日文 efont |
| `fonts::efontKR_10~24` | U8g2font | 韩文 efont |
| `fonts::efontTW_10~24` | U8g2font | 繁体中文 efont |

**使用中文**：
```cpp
M5.Display.setFont(&fonts::efontCN_16);  // 16px 简体中文
M5.Display.drawString("你好世界", 0, 0);
```

### 4.5 Sprite（M5Canvas = LGFX_Sprite）

| API | 签名 | 说明 | 来源 |
|---|---|---|---|
| `M5Canvas` | `M5Canvas(LovyanGFX* parent)` | 构造（PSRAM 优先） | `M5GFX.h:236` |
| `createSprite` | `void* createSprite(int32_t w, int32_t h)` | 分配 sprite 缓冲区 | `LGFX_Sprite.hpp:193` |
| `deleteSprite` | `void deleteSprite(void)` | 释放 sprite 缓冲区 | `LGFX_Sprite.hpp:144` |
| `pushSprite` | `void pushSprite(int32_t x, int32_t y)` | 推到父 Display | `LGFX_Sprite.hpp:347` |
| `pushSprite` | `void pushSprite(int32_t x, int32_t y, T transp)` | 推到父 Display（透明色） | `LGFX_Sprite.hpp:344` |
| `pushSprite` | `void pushSprite(LovyanGFX* dst, int32_t x, int32_t y)` | 推到指定目标 | `LGFX_Sprite.hpp:348` |
| `pushRotated` | `void pushRotated(float angle)` | 旋转推到父 Display | `LGFX_Sprite.hpp:352` |
| `pushRotateZoom` | `void pushRotateZoom(float angle, float zx, float zy)` | 旋转+缩放推 | `LGFX_Sprite.hpp:361` |
| `pushAffine` | `void pushAffine(const float matrix[6])` | 仿射变换推 | `LGFX_Sprite.hpp:368` |
| `setPsram` | `void setPsram(bool enabled)` | 切换 PSRAM/内部 RAM 分配 | `LGFX_Sprite.hpp:167-171` |
| `getBuffer` | `void* getBuffer(void) const` | 获取 sprite 缓冲区指针 | `LGFX_Sprite.hpp:131` |
| `setColorDepth` | `void* setColorDepth(uint8_t bpp)` | 改变 sprite 色深（会重建） | `LGFX_Sprite.hpp:315-322` |
| `fillSprite` | `void fillSprite(T color)` | 填充整个 sprite | `LGFX_Sprite.hpp:333` |
| `frameBuffer` | `void* frameBuffer(uint8_t)` | = `getBuffer()`（M5Canvas 旧 API） | `M5GFX.h:240` |

### 4.6 图片解码

| API | 签名 | 说明 | 来源 |
|---|---|---|---|
| `drawJpg` | `bool drawJpg(const uint8_t* data, uint32_t len, int32_t x=0, ...)` | 从内存解码 JPEG | `LGFXBase.hpp:961-965` |
| `drawJpgFile` | `bool drawJpgFile(const char* path, ...)` | 从文件系统解码 JPEG | `LGFXBase.hpp:949-958` |
| `drawPng` | `bool drawPng(const uint8_t* data, uint32_t len, ...)` | 从内存解码 PNG | `LGFXBase.hpp:969` |
| `drawPngFile` | `bool drawPngFile(const char* path, ...)` | 从文件系统解码 PNG | `LGFXBase.hpp:949-958` |
| `drawBmp` | `bool drawBmp(const uint8_t* data, uint32_t len, ...)` | 从内存解码 BMP | `LGFXBase.hpp:968` |
| `drawBmpFile` | `bool drawBmpFile(const char* path, ...)` | 从文件系统解码 BMP | `LGFXBase.hpp:949-958` |
| `drawQoi` | `bool drawQoi(const uint8_t* data, uint32_t len, ...)` | 从内存解码 QOI | `LGFXBase.hpp:970` |
| `drawQoiFile` | `bool drawQoiFile(const char* path, ...)` | 从文件系统解码 QOI | `LGFXBase.hpp:949-958` |

> 底层解码器：`src/lgfx/utility/lgfx_tjpgd.c`（JPEG）、`lgfx_pngle.c`（PNG）、`lgfx_qoi.c`（QOI）、`lgfx_miniz.c`（BMP/解压）。需要在 `#include <M5GFX.h>` 之前 `#include <SD.h>` 等。

### 4.7 pushImage / 像素操作

| API | 签名 | 说明 | 来源 |
|---|---|---|---|
| `pushImage` | `void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const T* data)` | 绘制像素数组 | `LGFXBase.hpp:653` |
| `pushImage` | `void pushImage(int32_t x, ..., const T1* data, const T2& transparent)` | 带透明色 | `LGFXBase.hpp:658` |
| `pushImageRotateZoom` | `void pushImageRotateZoom(float dst_x, ...)` | 旋转缩放绘制 | `LGFXBase.hpp:698` |
| `pushImageAffine` | `void pushImageAffine(const float matrix[6], ...)` | 仿射变换绘制 | `LGFXBase.hpp:735` |
| `readPixel` | `uint16_t readPixel(int32_t x, int32_t y)` | 读像素（RGB565） | `LGFXBase.hpp:616` |
| `readPixelRGB` | `RGBColor readPixelRGB(int32_t x, int32_t y)` | 读像素（RGB888） | `LGFXBase.hpp:625` |
| `readRect` | `void readRect(int32_t x, int32_t y, int32_t w, int32_t h, T* data)` | 读矩形区域 | `LGFXBase.hpp:646` |
| `copyRect` | `void copyRect(int32_t dst_x, int32_t dst_y, int32_t w, int32_t h, int32_t src_x, int32_t src_y)` | 屏幕内拷贝矩形 | `LGFXBase.hpp:660` |
| `scroll` | `void scroll(int_fast16_t dx, int_fast16_t dy=0)` | 滚动屏幕内容 | `LGFXBase.hpp:659` |

### 4.8 旋转与坐标

| API | 签名 | 说明 | 来源 |
|---|---|---|---|
| `setRotation` | `void setRotation(uint_fast8_t rotation)` | 设置屏幕旋转 0-7 | `LGFXBase.hpp:637` |
| `getRotation` | `uint8_t getRotation(void) const` | 获取当前旋转值 | `LGFXBase.hpp:636` |
| `width` | `int32_t width(void) const` | 当前方向下宽度（受 rotation 影响） | `LGFXBase.hpp:321` |
| `height` | `int32_t height(void) const` | 当前方向下高度（受 rotation 影响） | `LGFXBase.hpp:322` |
| `setClipRect` | `void setClipRect(int32_t x, int32_t y, int32_t w, int32_t h)` | 设置裁剪区域 | `LGFXBase.hpp:641` |
| `clearClipRect` | `void clearClipRect(void)` | 清除裁剪区域 | `LGFXBase.hpp:643` |

### 4.9 其他

| API | 签名 | 说明 | 来源 |
|---|---|---|---|
| `startWrite` | `void startWrite(bool transaction=true)` | 开始批量写入（加速） | `LGFXBase.hpp:143` |
| `endWrite` | `void endWrite(void)` | 结束批量写入 | `LGFXBase.hpp:144` |
| `progressBar` | `void progressBar(int x, int y, int w, int h, uint8_t val)` | 画进度条 | `M5GFX.h:247` |
| `pushState/popState` | `void pushState(void) / popState(void)` | 保存/恢复字体+光标 | `M5GFX.h:248-249` |
| `qrcode` | `void qrcode(const char* s, ...)` | 二维码绘制 | `LGFXBase.hpp:914` |
| `setEpdMode` | `void setEpdMode(epd_mode_t epd_mode)` | EPD（电子纸）模式 | `LGFXBase.hpp:1434` |

---

## 5. 易错点与反模式

### 5.1 ❌ 用 M5Unified 时再实例化一个 M5GFX

```cpp
// 错误！M5.begin() 已经初始化了 M5.Display
M5GFX myDisplay;
myDisplay.begin();  // 额外实例，与 M5.Display 是独立的
```

**正确**：直接使用 `M5.Display`。

### 5.2 ❌ 与 LovyanGFX 的关系

M5GFX **是** LovyanGFX 的 fork，不是 wrapper。`M5GFX.h:62` 的 `#define LGFX_USE_V1` 开启了 LovyanGFX v1 API。可以直接用 LovyanGFX API（如 `loadFont`、`drawJpg`），但**不要同时引入另一个 LovyanGFX 库版本**——会导致链接冲突。

### 5.3 ❌ setRotation 导致坐标"乱码"

StickS3/Cardputer 的 135×240 面板在 autodetect 时配置为 `rotation=0`（竖屏 135×240）。Cardputer 额外设置 `rotation=1`（横屏 240×135）。`setRotation` 改变的是**逻辑坐标系**，`width()`/`height()` 会随之对调：

| rotation | StickS3 物理方向 | width() | height() |
|---|---|---|---|
| 0（默认） | USB 在下方，竖屏 | 135 | 240 |
| 1 | USB 在右侧，横屏 | 240 | 135 |
| 2 | USB 在上方，竖屏（倒） | 135 | 240 |
| 3 | USB 在左侧，横屏（倒） | 240 | 135 |

**镜像/偏移问题**：StickS3 的 `offset_x=52, offset_y=40` 是面板内部的行列偏移，**不要手动改**。如果绘制区域不对（比如只画在左上角），检查 `setRotation` 值而非 offset。

### 5.4 ❌ Sprite 内存分配 —— 无 PSRAM 设备

**Cardputer-Adv（ESP32-S3FN8，无 PSRAM）** 上 `M5Canvas sprite(&M5.Display)` 默认启用了 `_psram = true`，`createSprite` 会尝试从 PSRAM 分配，失败返回 `nullptr`：

```cpp
// Cardputer-Adv 正确做法
M5Canvas sprite(&M5.Display);
sprite.setPsram(false);  // ← 必须！PSRAM 不存在
sprite.createSprite(135, 240);
```

**StickS3（8MB OPI PSRAM）** 可以用 PSRAM：
```cpp
M5Canvas sprite(&M5.Display);  // _psram = true 正常工作
sprite.createSprite(135, 240); // 从 PSRAM 分配
```

Sprite 内存计算（rgb565_2Byte，16-bit 色深）：
- 135×240 = 32,400 像素 × 2 bytes = **64,800 bytes ≈ 63.3 KB**
- 8-bit 色深：**32,400 bytes ≈ 31.6 KB**
- 1-bit 色深：**4,050 bytes ≈ 4 KB**

Cardputer-Adv 的 ESP32-S3FN8 内部 SRAM 约 512KB DRAM，通常够用，但大 sprite 需谨慎。

### 5.5 ❌ 中文显示乱码

默认 `Font0`（GLCD 6×8）只含 ASCII 0x20-0x7E。中文需要用：

```cpp
// 方式1：U8g2 内置中文字体
M5.Display.setFont(&fonts::efontCN_16);
M5.Display.drawString("你好", 0, 0);

// 方式2：VLW 字体文件（从 SD 卡加载）
M5.Display.loadFont("/DejaVuSans24.vlw");
// VLW 字体需要包含 CJK 字符

// 方式3：efont 自定义（需 #include efont 数据头）
// 见 examples/LvglFont/
```

### 5.6 ❌ 双缓冲 misconception

M5GFX **没有内置双缓冲**。`M5Canvas`（`LGFX_Sprite`）是离屏缓冲区，需要手动 `pushSprite` 推到屏幕。正确的"双缓冲"模式：

```cpp
M5Canvas canvas(&M5.Display);
canvas.createSprite(135, 240);
// 在 canvas 上绘制...
canvas.pushSprite(0, 0);  // 一次性推到屏幕
```

### 5.7 ❌ begin/init 调用顺序

使用 M5Unified 时：
```cpp
// 正确顺序
M5.begin(cfg);          // 这里已经初始化了 Display
M5.Display.setRotation(1);
M5.Display.fillScreen(TFT_BLACK);

// 错误：M5.begin() 之后不要再次调用
// M5.Display.begin();   // ← 多余！getBoard() 已有值会跳过
```

### 5.8 ❌ setColorDepth 用错

`setColorDepth` 在屏幕上通常无效（ST7789 固定 16-bit）。它主要用于 **Sprite**（创建 sprite 前设置）：
```cpp
M5Canvas sprite(&M5.Display);
sprite.setColorDepth(8);   // 8-bit 调色板模式
sprite.createSprite(100, 100);
```

### 5.9 ❌ 卡在 autodetect 无限重试

autodetect 最多重试 4 次，每次都会尝试 I2C/SPI 通信。如果设备没有连接面板（如裸 ATOM Lite），最后会返回 `board_unknown` 且 `init_impl` 返回 `true`（无面板时不算失败）。此时 `getBoard()` 为 `board_unknown`，后续绘制操作会空操作。

### 5.10 ❌ `using namespace lgfx` 与 Arduino `millis()` 冲突

`lgfx::v1` 内有自己的 `millis()`（`platforms/esp32/common.hpp:118`）。在 sketch 里写 `using namespace lgfx;` 后调用裸 `millis()` 会编译失败：

```
error: call of overloaded 'millis()' is ambiguous
note: candidate: 'long unsigned int millis()'          // esp32-hal.h
note: candidate: 'long unsigned int lgfx::v1::millis()' // common.hpp
```

**原因**：`using namespace lgfx` 把 `lgfx::v1::millis` 引入全局，与 Arduino 全局 `millis` 重载冲突（2026-08-18 在 liangzi-meter 实测）。

**正确做法**：不需要 `using namespace lgfx` —— `fonts::efontCN_16`（`lgfx_fonts.hpp:574-577` 有全局别名 `namespace fonts`）与 `middle_center`/`middle_left` 等 datum 常量（`misc/enum.hpp:495` 全局 `using namespace lgfx::textdatum;`）**都已在全局作用域可直接使用**。同理 `TFT_*` 颜色也已被 `using namespace m5gfx::ili9341_colors;` 全局导出。需要 lgfx 内部符号时用显式限定（如 `lgfx::v1::...`），不要整体 `using namespace lgfx`。

---

## 6. 版本兼容

| 项目 | 值 |
|---|---|
| 本地版本 | **M5GFX 0.2.26** |
| StickS3 要求 | ≥ 0.2.18（来自 kb/m5stick-s3.md） |
| Cardputer-Adv 要求 | ≥ 0.2.10（来自 kb/cardputer-adv.md） |
| M5Unified 依赖 | M5GFX 作为 M5Unified 的子模块/Library Manager 依赖 |

> 0.2.26 向后兼容所有 M5Stack 设备。`board_M5StickS3 = 26`、`board_M5CardputerADV = 24` 在该版本中已存在（`src/lgfx/boards.hpp` 核实）。

---

## 7. 最小可编译示例

### 7.1 通过 M5Unified（推荐）

```cpp
#include <M5Unified.h>

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setCursor(0, 0);
    M5.Display.println("Hello M5GFX!");
    M5.Display.printf("Board: %d\n", (int)M5.Display.getBoard());
}

void loop() {
    M5.update();
}
```

### 7.2 独立使用 M5GFX

```cpp
#include <M5GFX.h>

M5GFX display;

void setup() {
    display.begin();
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.drawString("Standalone M5GFX", 10, 10);
}

void loop() {}
```

### 7.3 Sprite 示例（Cardputer-Adv 安全版）

```cpp
#include <M5Unified.h>

M5Canvas sprite(&M5.Display);

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    sprite.setPsram(false);               // Cardputer-Adv 无 PSRAM
    sprite.createSprite(135, 240);
    sprite.fillSprite(TFT_BLACK);
    sprite.setTextColor(TFT_WHITE);
    sprite.drawString("Sprite OK", 10, 10);
    sprite.pushSprite(0, 0);
}

void loop() { M5.update(); }
```

### 7.4 中文显示示例

```cpp
#include <M5Unified.h>

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.drawString("你好世界", 0, 0);
}

void loop() { M5.update(); }
```

---

## 8. 参考链接

- **M5GFX GitHub**：https://github.com/m5stack/M5GFX
- **LovyanGFX 原版**：https://github.com/lovyan03/LovyanGFX
- **M5Unified**：https://github.com/m5stack/M5Unified
- **m5stack-board-id**（板型 ID 注册表）：https://github.com/m5stack/m5stack-board-id/blob/main/board.csv
- **StickS3 KB**：`kb/m5stick-s3.md`
- **Cardputer-Adv KB**：`kb/cardputer-adv.md`

> 文件生成时间：2026-08-03 | 核实工具：source read / grep | 本地库路径：`<libraries>\M5GFX`
