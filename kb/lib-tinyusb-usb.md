# TinyUSB 实战 KB：MSC 虚拟 U 盘 + HID Vendor 双向通道

> **来源**：本机 m5stack:esp32 core 3.3.8 源码 + 2026-08-16 真机实测（StickS3，w96p-remote/tools/usb-vfs 与 usb-hid 两个原型）。所有坑均为**真机验证过的结论**，非推测。
>
> 适用：ESP32-S3（StickS3 / Cardputer-Adv），core 3.x。相关代码见 `w96p-remote/tools/usb-vfs/`（MSC）、`w96p-remote/tools/usb-hid/`（HID Vendor）。

## 1. 硬件前提：USB 口是"串口 / OTG"二选一

ESP32-S3 的 USB D+/D-（GPIO19/20）同时接 **USB Serial/JTAG**（烧录+串口日志）和 **USB-OTG 全速控制器**（TinyUSB），**运行时只能启用一个**：

| 模式 | `ARDUINO_USB_MODE` | 行为 |
|---|---|---|
| Hardware CDC/JTAG（默认） | `1` | 串口监视器可用；**TinyUSB 不可用** |
| USB-OTG (TinyUSB) | `0` | TinyUSB 栈启用；串口监视器消失，日志改走 TinyUSB CDC |

启用方式（arduino-cli）：`--fqbn m5stack:esp32:m5stack_sticks3:USBMode=default,CDCOnBoot=cdc`
- `USBMode=default` → `build.usb_mode=0` → `-DARDUINO_USB_MODE=0`（platform.txt:79 映射）
- `CDCOnBoot=cdc` → TinyUSB CDC 与你的自定义接口**复合共存**（保留串口日志）
- 烧录不受影响：复位进 ROM bootloader 走 USB Serial/JTAG

**编译保护**：sketch 开头加 `#if !defined(ARDUINO_USB_MODE) || ARDUINO_USB_MODE == 1` + `#error`，防止误用默认 FQBN 编译出怪问题。

## 2. USBMSC 虚拟 U 盘（把设备变成只读 U 盘）

API：`USBMSC`（core 内置，`src/USBMSC.h`）：`begin(block_count, block_size)` + `onRead/onWrite/onStartStop` + `isWritable(false)` + `mediaPresent(true)`。回调签名：

```cpp
typedef int32_t (*msc_read_cb)(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize);
typedef int32_t (*msc_write_cb)(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize);
```

### 坑 1（必修）：READ10 是跨扇区请求，onRead 必须逐扇区处理

**Windows 的 READ10 一次请求多个扇区（bufsize 可达 4KB+），TinyUSB 把整个请求透传给 onRead 回调**。若把 `bufsize > 512` 的请求 return 0：

- 实测后果：**每秒 21 万次 READ 失败 → USB 总线风暴 → 主机 BUS_RESET → 设备断开重连循环**（System 日志 `disk` Event 11，SRB 状态 `0x000E` = BUS_RESET）
- 修复：逐扇区拆分填充，只有"请求超出卷容量"才拒绝（返回已读部分而非 0）：

```cpp
static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  uint8_t *dst = (uint8_t *)buffer;
  if (offset >= 512) { lba += offset / 512; offset %= 512; }
  uint32_t done = 0;
  while (done < bufsize) {
    if (lba >= BLOCK_COUNT) break;
    uint32_t n = min(512u - offset, bufsize - done);
    memcpy(dst + done, disk[lba] + offset, n);
    done += n; lba++; offset = 0;
  }
  return done;
}
```

### 坑 2（实测结论）：Windows 挂载后不再读盘，设备端无法"实时刷新"

- **现象**：固件每 500ms 刷新镜像里的 STATUS.TXT，但主机重复读文件拿到的是**挂载时刻缓存**（内容 20s 不变）
- **根因**：Windows 对已挂载卷"空闲"时**不发任何 SCSI 命令**（连 TEST UNIT READY 都不轮询）→ 设备端任何"被动通知"（SCSI Unit Attention 等）都无路可送
- **实测**：`tud_msc_set_sense(0, SCSI_SENSE_UNIT_ATTENTION, 0x28, 0x00)` 每 3s 发出，但主机 `reads=+0`、文件内容不变——**UA 方案在 Windows 上无效**（社区 PicoVD 等方案依赖主机持续轮询，Windows 不满足）
- **结论**：Windows 下 MSC 只读盘只能做"挂载时刻快照"（拔插刷新）。要设备主动推送状态，**必须走 HID IN report（设备主动发）**，MSC 做不到
- 补充：BPB **卷序列号每次上电随机化**（`esp_random()`）可避免 Windows 跨连接命中旧缓存

### FAT12 手写要点（运行时构建内存镜像）

- 单簇文件：目录项起始簇的 FAT 项必须是 `0xFFF`（EOC）；**空闲簇才是 `0x000`**——反了会导致 chkdsk/读取异常
- 目录项 name 域固定 8 字节 / ext 3 字节，空格补齐（C++ 用 `memset(e,' ',8)` + `strnlen` 防手数空格错误）
- 卷最小 8KB（16 扇区）即可挂载；32KB（64 扇区）实测稳定

## 3. USBHIDVendor 双向通道（推荐，替代 MSC）

`USBHIDVendor`（core 内置，`src/USBHIDVendor.h`）：**继承 Stream**，设备端像串口一样 `print/println/read/available`；report descriptor 是 `TUD_HID_REPORT_DESC_GENERIC_INOUT_FEATURE`，IN/OUT/FEATURE 共用 **report id 6**（`HID_REPORT_ID_VENDOR`），usage page `0xFF00`，Windows 免驱。

```cpp
USBHIDVendor Vendor;
Vendor.begin(); USB.begin();
Vendor.println("S:65 G:3 P:1");      // IN report：推状态（设备主动，无缓存问题）
// 收命令见坑 3
```

### 坑 3（必修）：带 report id 的 OUTPUT report 走 feature 分支，命令收不到

**arduino-esp32 的 `tud_hid_set_report_cb`（src/USBHID.cpp:263）把"带 report id 的 OUTPUT report"路由到 `tinyusb_on_set_feature`**，而 `USBHIDVendor::_onOutput` 永远不会被调用 → `Vendor.available()/read()` 永远读不到主机发的 OUT 命令。主机 `write()` 发的命令全部丢失（实测固件 RX 为空、命令解析报 bad cmd 或静默）。

**解法：命令走 FEATURE report 通道**（描述符本来就支持）：

```cpp
// 固件：订阅 SET_FEATURE 事件取数据（事件回调里只拷贝，loop 里解析）
static char featureCmd[64];
static volatile bool featurePending = false;
static void vendorEventCallback(void *arg, esp_event_base_t base, int32_t id, void *data) {
  if (base != ARDUINO_USB_HID_VENDOR_EVENTS) return;
  if (id == ARDUINO_USB_HID_VENDOR_SET_FEATURE_EVENT) {
    auto *p = (arduino_usb_hid_vendor_event_data_t *)data;
    size_t n = min((size_t)p->len, sizeof(featureCmd) - 1);
    memcpy(featureCmd, p->buffer, n); featureCmd[n] = '\0';
    featurePending = true;
  }
}
// setup: Vendor.onEvent(vendorEventCallback);
// loop:  若 featurePending → strpbrk(featureCmd, "\r\n") 截断 → handleCommand(featureCmd)
```

```python
# 主机（Python hidapi）：用 send_feature_report，首字节 = report id 6
dev.send_feature_report(b'\x06' + b'SPD 65\r\n')
```

## 4. 主机侧工具与调试方法（Windows）

### cffi hid 包（`uv run --with hidapi python ...`）

- API：`hid.device()`（**小写**类）→ `dev.open(vid, pid)` → `dev.read(64, timeout_ms)` → `dev.send_feature_report(data)`
- **`read()` 返回 int 列表，首字节恒为 report id（=6）**，取数据要 `bytes(data[1:])`
- `write()` 返回 64（Windows 自动补 0 到完整 report 长度）
- 枚举：`hid.enumerate()` 按 `vendor_id` + `usage_page == 0xFF00` 过滤

### 排查 USB 断连/枚举问题

```bash
# 抓固件 CDC 日志（固件里回调禁 Serial 输出，只计数 + loop 限频打印；回调上下文 printf 会阻塞 USB 任务）
uv run --with pyserial python - <<'PY'
import serial, time
s = serial.Serial('COM3', 115200, timeout=0.2)
t0 = time.time()
while time.time() - t0 < 10:
    d = s.read(4096)
    if d: print(d.decode('utf-8','replace'), end='')
PY

# 查 Windows 磁盘错误事件（Event 11 = 控制器错误/BUS_RESET，DR 号递增 = 断连循环）
wevtutil qe System /q:"*[System[(Provider[@Name='disk']) and (Level=2)]]" /c:10 /rd:true /f:text

# 枚举盘符（ctypes 坑：GetLogicalDriveStringsW 必须声明 argtypes/restype，
# create_unicode_buffer.value 在首个 \0 截断，要取 buf[:n]）
```

**固件侧诊断模式**：MSC 回调里 `volatile` 计数器（reads/fails/starts/writes），loop 每秒 `Serial.printf` 摘要——`fails` 爆炸 = 回调被错误拒绝；`reads=0` = 主机没在读盘（缓存/空闲）。

## 5. WebHID 方向（浏览器控制，固件零改动）

- **WebUSB 访问不了 HID 设备**（HID 是浏览器受保护 class，只能走 `navigator.hid` = WebHID，Chrome 89+/Edge）
- 现有 usb-hid 固件可直接复用：`requestDevice({filters:[{vendorId:0x303A}]})` → `inputreport` 事件收状态流 → `sendFeatureReport([0x06, ...])` 发命令
- WebUSB（`navigator.usb`）需要 vendor class 固件（`USBVendor`），且 Windows 上**必须实现 MS OS 2.0 描述符才免驱**（arduino-esp32 默认不带）——非必要别走

## 6. 一句话结论

| 需求 | 方案 |
|---|---|
| 免驱、设备主动推状态、双向控制 | **USBHIDVendor + FEATURE report 命令通道**（首选） |
| 真"一切皆文件"体验（只读状态快照） | USBMSC（接受"挂载时刻快照"，拔插刷新） |
| 浏览器控制 | WebHID（`navigator.hid`），复用 HID 固件 |
