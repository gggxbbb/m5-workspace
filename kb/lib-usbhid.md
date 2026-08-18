# USB HID 键盘 KB（Arduino-ESP32 core 自带）

> **来源权威**：核实自本机 m5stack:esp32 core 3.3.8 源码 `<arduino-data>\packages\m5stack\hardware\esp32\3.3.8\libraries\USB\src\`。
>
> 适用：ESP32-S3 系列（**StickS3 / Cardputer / Cardputer-Adv** 原生 USB 口），core 3.x（3.3.8 验证）。M5Unified **不含** HID 支持，USB HID 键盘走 Arduino core 自带的 `USBHIDKeyboard`。

## 1. 定位

ESP32-S3 内置 USB-OTG，Arduino core 用 TinyUSB 暴露 USB 设备接口。`USBHIDKeyboard`（`src/USBHIDKeyboard.h`）将设备伪装成标准 HID 键盘，无需任何外部硬件即可向电脑发送按键。**与 M5Unified 的 `M5.begin()` 完全兼容**：TinyUSB 多接口共存（CDC 串口 + HID 键盘），`M5.begin()` 初始化 CDC，`kbd.begin()` 追加 HID 接口。

## 2. 最小用法

```cpp
#include <M5Unified.h>
#include <USB.h>
#include <USBHIDKeyboard.h>

USBHIDKeyboard kbd;

void setup() {
  M5.begin();
  kbd.begin();          // 内部自动调 USB.begin()（幂等），无需手动
  delay(2000);          // 等 Windows 枚举并识别键盘，否则首帧会丢
}

void sendKey(uint8_t key, uint32_t hold = 50) {
  kbd.press(key);
  delay(hold);
  kbd.release(key);
  delay(30);
}
```

## 3. API 与按键码（`USBHIDKeyboard.h` 核实）

| API | 说明 |
|---|---|
| `begin(layout = KeyboardLayout_en_US)` | 注册 HID 接口；`end()` 注销 |
| `press(k)` / `release(k)` / `releaseAll()` | 按/松/全松；可同时按多键（最多 6 个普通键 + 修饰键） |
| `write(k)` | 一次性按下并释放（继承 `Print`，走同一 asciimap） |
| `pressRaw(k)` / `releaseRaw(k)` | 直接传 TinyUSB `HID_KEY_*` 原始键码，绕过 asciimap |
| `sendReport(KeyReport*)` | 底层裸上报 |

**按键码规则**：
- `0x00–0x7F`：ASCII 字符，经 `_asciimap`（默认 en_US 布局）转成 HID 键码。`press('x')`、`press('u')` 直接传字符即可。
- `0x80–0x87`：修饰键宏 `KEY_LEFT_CTRL(0x80) / KEY_LEFT_SHIFT(0x81) / KEY_LEFT_ALT(0x82) / KEY_LEFT_GUI(0x83)` 及右侧对应。
- `0xB0+`：功能/导航键宏（`KEY_RETURN 0xB0`、`KEY_TAB 0xB3`、`KEY_F1–F24 0xC2–`、`KEY_DELETE 0xD4`、`KEY_MENU 0xED` 等）。**没有** `KEY_X/KEY_U` 这类字母宏——字母直接用 ASCII。

组合键标准写法：先 `press` 修饰键，再 `press` 普通键，`delay` 保持，最后 `releaseAll()`（参考 `usb-poweroff/` 固件）。

## 4. 已编译验证的典型场景：Windows 关机快捷键

`usb-poweroff/usb-poweroff.ino`（StickS3 + Cardputer-Adv 双 FQBN 编译通过）发送 `Win+X → U → U`：

```cpp
kbd.press(KEY_LEFT_GUI); kbd.press('x');   // Win+X 快速链接菜单
delay(500); kbd.releaseAll();              // 等菜单弹出
sendKey('u');                              // "关机或注销"
delay(400);
sendKey('u');                              // "关机"
```

要点与坑：
1. **上电必须延时再发**：Windows 枚举 HID 需 1–2 秒，setup 里立刻发首帧会丢。
2. **锁屏/登录界面下 Win+X 无效**，系统须已登录且桌面可见。
3. **烧录后设备自动复位会再次触发**——上电即自动发键的固件，调试时要么改宏关闭自动触发，要么上电按住按钮跳过（固件里做了 3 秒逃生舱）。
4. `press('u')` 的字母键不受 CapsLock/布局影响（发的是键码不是字符），中文 Windows 的 Win+X 热键同为 U，通用。
5. 其它关机途径备选：`Win+D → Alt+F4 → Enter`（需桌面焦点）；`Win+R → "shutdown /s /t 0" → Enter`（可靠但按键多、依赖输入法）。

## 5. 约束与反模式

- 仅 `SOC_USB_OTG_SUPPORTED` 的芯片（S3 系）编译；`CONFIG_TINYUSB_HID_ENABLED` 需开启（core 默认开）。
- 不要与 M5Cardputer 的键盘输入混淆：Cardputer 自带键盘是**设备侧输入**（TCA8418 读矩阵），USBHIDKeyboard 是**主机侧输出**（向电脑发键），两者无冲突。
- 一个 USBHIDKeyboard 实例同时按下普通键上限 6 个，修饰键不算在内；超出部分静默丢弃。
- `write()` 对多字节 UTF-8 字符不支持，仅单字节 ASCII 布局映射。
