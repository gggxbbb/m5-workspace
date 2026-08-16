# USB VFS（一切皆文件）· 设计规格

> 状态：**已定稿，实施中**。目标设备：M5StickS3（K150）。可行性已核实（见 §2），本文档为设计与验收依据。
>
> 一句话：把遥控器的 USB-C 变成一块只读 U 盘，盘里 `STATUS.TXT` 实时反映风扇状态——`cat STATUS.TXT` 即读当前快照，近似 Linux procfs 体验。

---

## 1. 目标与范围

### 1.1 目标

在 ESP32-S3 的 USB-C 上枚举为 **USB Mass Storage（虚拟 U 盘）**，通过文件暴露 W96P 风扇状态信息，实现"一切皆文件"的最小闭环。

### 1.2 本次实施范围（MVP，只读）

| 项 | 内容 |
|---|---|
| USB 设备 | TinyUSB MSC，只读（`isWritable(false)`） |
| 卷 | 32KB FAT12 内存盘（64 扇区 × 512B），镜像运行时构建 |
| 文件 | `STATUS.TXT`（动态刷新）、`README.TXT`（静态说明） |
| 状态源 | MOCK 快照（500ms 变化模拟）；上真机后替换为 `w96p::Snapshot` |
| 交付 | `tools/usb-vfs/usb-vfs.ino` + 编译通过 |

### 1.3 二期（本次不做，设计预留）

- **可写控制文件**：`SPEED` / `POWER` / `TIMER`，写文件 → 解析内容 → BLE 命令下发（需 SCSI WRITE10 拦截 + FAT 定位）
- **LOG/ 日志落盘**：USB 模式下替代串口监视器（TinyUSB 启用后硬件 Serial/JTAG 不可用）
- **Cardputer-Adv 支持**：无 PSRAM，卷镜像改放 flash（LittleFS）或缩至 128KB 塞 SRAM

---

## 2. 可行性结论（已核实，2026-08-16）

**结论：可行**。`arduino-esp32` core 3.3.8（`m5stack:esp32`）内置 TinyUSB + `USBMSC` 类，官方自带 `USB/examples/USBMSC` 例程（手写 FAT12 镜像 + onRead/onWrite 回调），本方案直接以此为模板。

### 2.1 已核实技术事实

| 事实 | 证据 |
|---|---|
| `USBMSC` 类存在 | `cores/esp32/USBMSC.h`：`begin(block_count, block_size)`、`onRead(lba, offset, buffer, bufsize)`、`onWrite`、`onStartStop`、`isWritable(bool)`、`mediaPresent(bool)` |
| 官方例程可作模板 | `libraries/USB/examples/USBMSC/USBMSC.ino`：手写 FAT12 镜像，8KB（16 扇区）即最小可挂载 |
| StickS3 USB 默认模式 | `boards.txt`：`m5stack_sticks3.build.usb_mode=1` = Hardware CDC/JTAG（当前串口日志走它） |
| 切 TinyUSB 方式 | board 菜单 `USBMode=default` → `build.usb_mode=0` → `-DARDUINO_USB_MODE=0`（platform.txt:79） |
| 保留串口日志 | `CDCOnBoot=cdc` → `-DARDUINO_USB_CDC_ON_BOOT=1` → TinyUSB CDC 与 MSC 复合共存 |
| 烧录不受影响 | 复位进 ROM bootloader 走 USB Serial/JTAG，与运行时代码无关 |

### 2.2 关键硬件约束（违反即翻车）

1. **GPIO19/20 二选一**：USB Serial/JTAG（日志/监视器）与 USB-OTG（TinyUSB）运行时互斥。启用 TinyUSB 后硬件串口监视器不可用，日志只能走 TinyUSB CDC（`CDCOnBoot=cdc`）或文件。
2. **StickS3 有 8MB OPI PSRAM**：32KB 镜像直接放 PSRAM 或 SRAM 均可，无压力。Cardputer-Adv 无 PSRAM，见 §1.3。

---

## 3. 架构

```
PC 主机 (资源管理器 / cat / 脚本)
   │  USB-C 全速 12Mbps (GPIO19/20)
   ▼
TinyUSB MSC 设备  ←— SCSI READ(10)/WRITE(10)
   │
   ▼
32KB FAT12 内存盘 (uint8_t disk[64][512], 运行时构建)
   │
   ▼
文件语义层:
   STATUS.TXT  ← 每 500ms 由快照 tick 重写内容 (procfs 风格: 读即最新)
   README.TXT  ← 静态说明
   │
   ▼
w96p client (500ms 轮询快照) ──BLE──► W96P 风扇
```

**实现要点**：采用**轮询刷新**而非"读时生成"——快照 tick 直接把新内容写进镜像的 STATUS 数据扇区并更新目录项文件大小；`onRead` 纯 `memcpy`。规避主机缓存问题：Windows 对可移动盘一般每次重新读介质，实测验证（§8）。

---

## 4. 虚拟 FAT12 卷设计（32KB）

### 4.1 布局（64 扇区 × 512B，每簇 1 扇区）

| 扇区 | 内容 |
|---|---|
| 0 | 引导扇区（BPB + 0x55AA） |
| 1 | FAT 表（63 项 FAT12，占 95 字节，余 0x00） |
| 2 | 根目录（16 项：卷标 + README.TXT + STATUS.TXT + 13 空） |
| 3-10 | STATUS.TXT 数据（**固定 8 簇**，容量 4KB，FAT 链 3→4→…→10） |
| 11 | README.TXT 数据（簇 11，1 簇） |
| 12-63 | 空闲簇 12-62 |

簇号 = 扇区号 - 1（数据区从扇区 3 起，簇 2 起）。

### 4.2 BPB 关键字段

```text
bytes_per_sector=512  sectors_per_cluster=1  reserved=1
num_fats=1  root_entries=16  total_sectors=64
media=0xF8  sectors_per_fat=1  boot_sig=0x55AA
```

### 4.3 FAT 表

```text
簇 0:0xFF8  簇 1:0xFFF  簇 2:0xFFF(README 结束)
簇 3-9: 4,5,6,7,8,9,10  (STATUS 链)
簇 10:0xFFF  其余:0x000
```

FAT12 逐项 3 字节打包（运行时循环构建，偶/奇索引分写）。

### 4.4 STATUS.TXT 动态刷新规则

- 分配容量 4KB（8 簇），实际内容 ≤ 4KB
- 每次刷新：写内容到簇 3-10，内容不足的扇区补 `0x20`（空格）
- 目录项文件大小字段（偏移 28-31）同步更新为实际长度
- **FAT 链不动**（固定分配），避免写 FAT 表引入的并发/一致性风险

### 4.5 STATUS.TXT 内容格式（MOCK 版）

```text
W96P USB VFS MOCK
SPD  65%
GEAR 3
POW  ON
BAT  3.95V  -412mA
MOT  320mA
TUR  0s
TS  1726389600
```

上真机后替换为 `w96p::Snapshot` 真实字段（采信原则同 sticks3 设计：只信电压/电流）。

---

## 5. 编译与烧录

### 5.1 编译（StickS3，TinyUSB OTG 模式 + CDC 日志）

```bash
arduino-cli compile --libraries ./w96p-remote/lib \
  --fqbn m5stack:esp32:m5stack_sticks3:USBMode=default,CDCOnBoot=cdc \
  w96p-remote/tools/usb-vfs
```

> 注意：**必须** `USBMode=default`，否则 `ARDUINO_USB_MODE=1` 时 sketch 直接 `#warning` 跳过（例程行为）。

### 5.2 烧录

```bash
arduino-cli upload --fqbn m5stack:esp32:m5stack_sticks3:USBMode=default,CDCOnBoot=cdc -p COMx w96p-remote/tools/usb-vfs
```

设备需进下载模式（按侧边复位 ~2s 至绿 LED 闪烁）。

---

## 6. 文件布局（用户视角）

```
USB VFS (32KB 可移动磁盘)
├── README.TXT   说明文件
└── STATUS.TXT   实时状态（每次打开读到最新）
```

---

## 7. 实施任务清单（本次）

1. **写 spec**（本文档）
2. **实现 `tools/usb-vfs/usb-vfs.ino`**：
   - 运行时构建 FAT12 镜像（boot sector / FAT / 根目录 / README 内容）
   - `onRead` 纯 memcpy；`onWrite` 打印拒绝；`isWritable(false)`
   - loop：500ms tick 生成 MOCK 快照 → 刷新 STATUS.TXT
   - 串口打印 USB 插拔事件（`ARDUINO_USB_*_EVENT`）
3. **编译验证**（§5.1 命令，零错误）

## 8. 真机验收清单（上真机时逐项勾）

- [ ] Windows 资源管理器识别"USB VFS"可移动磁盘（32KB 应正常挂载）
- [ ] 磁盘属性为只读；尝试删除文件被拒绝
- [ ] 打开 `STATUS.TXT` 见当前快照；关闭 1-2 秒重开，值已变化（轮询刷新生效）
- [ ] `cat STATUS.TXT`（Linux/脚本）连续两次读到不同值
- [ ] 拔插 USB-C 重新枚举正常
- [ ] 设备管理器看到复合设备：CDC 串口 + 磁盘（`CDCOnBoot=cdc`）
- [ ] 串口监视器（TinyUSB CDC）能看到 USB 插拔日志

## 9. 风险与已知限制

| 风险 | 说明与对策 |
|---|---|
| Windows 文件缓存 | 动态 STATUS.TXT 可能被资源管理器缓存；实测 §8，若缓存则改"读时生成"（onRead 拦截 STATUS 扇区实时填充） |
| FAT 结构合法性 | 镜像由代码构建，任何字段错位都导致"需要格式化"；开发期用十六进制比对 / 真机挂载验证 |
| CONFIG_TINYUSB_MSC_ENABLED 未开 | USBMSC 类会被 `#if` 剔除导致编译失败；若出现需在 sdkconfig 显式启用 |
| 8.3 文件名 | FAT12 不支持长文件名，`STATUS.TXT` 等均为 8.3 短名 |
| 只读盘主机体验 | 部分 Windows 版本对只读可移动盘有额外提示，属正常 |

## 10. 二期规划（范围外，设计预留）

- **档 3 可写控制文件**：SCSI WRITE10 → 定位 LBA 属于哪个控制文件 → 解析内容 → `setSpeed/setPower/setTimerMinutes`；需维护 FAT 的 LBA→文件映射表
- **LOG.TXT**：TinyUSB 模式下替代串口监视器的日志出口
- **Cardputer-Adv**：镜像后端切换为 flash（LittleFS 或 spi_flash mmap），容量缩至 128KB
- **真实快照接入**：替换 MOCK 为 `w96p::Snapshot`，注意 USB 刷新与 BLE 轮询的并发（同一 task 内串行即可）
