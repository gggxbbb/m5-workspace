// usb-vfs.ino — USB "一切皆文件" 可行性实验 (M5StickS3)
// 设计: w96p-remote/docs/usb-vfs-design.md
//
// 把 USB-C 变成只读虚拟 U 盘 (TinyUSB MSC + 32KB FAT12 内存盘):
//   STATUS.TXT  每 500ms 由 MOCK 快照动态刷新 (procfs 风格: 读即最新)
//   README.TXT  静态说明
//
// 编译(必须 USBMode=default 切 OTG, 否则 #error):
//   arduino-cli compile --libraries ./w96p-remote/lib \
//     --fqbn m5stack:esp32:m5stack_sticks3:USBMode=default,CDCOnBoot=cdc \
//     w96p-remote/tools/usb-vfs
//
// 上真机后: 把 buildStatusText() 的 MOCK 替换为 w96p::Snapshot 字段。

#include <Arduino.h>
#include <esp_event.h>
#include <cstring>
#include <cstdio>
#include <cinttypes>

#include "USB.h"
#include "USBMSC.h"

#if !defined(ARDUINO_USB_MODE) || ARDUINO_USB_MODE == 1
#error "usb-vfs requires USB-OTG (TinyUSB): compile with USBMode=default"
#endif

// ============================== 卷参数 ==============================
static constexpr uint32_t DISK_SECTOR_COUNT  = 64;    // 32KB
static constexpr uint16_t DISK_SECTOR_SIZE   = 512;
static constexpr uint16_t ROOT_ENTRIES       = 16;
static constexpr uint16_t STATUS_FIRST_CLUST = 3;     // STATUS.TXT 起始簇
static constexpr uint16_t STATUS_CLUSTERS    = 8;     // 固定 8 簇 = 4KB 容量
static constexpr uint16_t README_CLUSTER     = 11;    // README.TXT 簇
static constexpr uint16_t STATUS_SECTOR0     = STATUS_FIRST_CLUST + 1; // 簇3=扇区4
static constexpr uint16_t README_SECTOR      = README_CLUSTER + 1;     // 簇11=扇区12
static constexpr uint32_t STATUS_CAPACITY    = STATUS_CLUSTERS * DISK_SECTOR_SIZE;

static uint8_t disk[DISK_SECTOR_COUNT][DISK_SECTOR_SIZE];  // FAT12 内存盘
static USBMSC  MSC;

static const char README_CONTENTS[] =
  "W96P USB VFS demo\r\n"
  "STATUS.TXT = live fan snapshot (refresh 500ms)\r\n"
  "This disk is READ-ONLY.\r\n";

// ============================== FAT12 小工具 ==============================
static void fat12_set(uint8_t *fat, uint16_t idx, uint16_t v) {
  size_t off = idx + (idx >> 1);                    // idx * 1.5
  if (idx & 1) {
    fat[off]     = (fat[off] & 0x0F) | ((v << 4) & 0xF0);
    fat[off + 1] = (v >> 4) & 0xFF;
  } else {
    fat[off]     = v & 0xFF;
    fat[off + 1] = (fat[off + 1] & 0xF0) | ((v >> 4) & 0x0F);
  }
}

#define FAT_U8(v)          ((v) & 0xFF)
#define FAT_U16(v)         FAT_U8(v), FAT_U8((v) >> 8)
#define FAT_U32(v)         FAT_U8(v), FAT_U8((v) >> 8), FAT_U8((v) >> 16), FAT_U8((v) >> 24)
#define FAT_MS2B(s, ms)    FAT_U8(((((s) & 0x1) * 1000) + (ms)) / 10)
#define FAT_HMS2B(h, m, s) FAT_U8(((s) >> 1) | (((m) & 0x7) << 5)), FAT_U8((((m) >> 3) & 0x7) | ((h) << 3))
#define FAT_YMD2B(y, m, d) FAT_U8(((d) & 0x1F) | (((m) & 0x7) << 5)), FAT_U8((((m) >> 3) & 0x1) | ((((y) - 1980) & 0x7F) << 1))

// ============================== 镜像构建 ==============================
static void buildBootSector() {
  uint8_t *b = disk[0];
  memset(b, 0, DISK_SECTOR_SIZE);
  b[0] = 0xEB; b[1] = 0x3C; b[2] = 0x90;
  memcpy(b + 3, "MSDOS5.0", 8);
  b[11] = FAT_U8(DISK_SECTOR_SIZE); b[12] = FAT_U8(DISK_SECTOR_SIZE >> 8);  // 512
  b[13] = 1;                                   // sectors/cluster
  b[14] = 1; b[15] = 0;                        // reserved
  b[16] = 1;                                   // FATs
  b[17] = FAT_U8(ROOT_ENTRIES); b[18] = FAT_U8(ROOT_ENTRIES >> 8);           // 16
  b[19] = FAT_U8(DISK_SECTOR_COUNT); b[20] = FAT_U8(DISK_SECTOR_COUNT >> 8); // 64
  b[21] = 0xF8;                                // media
  b[22] = 1; b[23] = 0;                        // sectors/FAT
  b[24] = 1; b[25] = 0;                        // sectors/track (LBA-only)
  b[26] = 1; b[27] = 0;                        // heads
  b[38] = 0x29;                                // extended boot sig
  b[39] = 0x12; b[40] = 0x34; b[41] = 0x56; b[42] = 0x78;  // volume id
  memcpy(b + 43, "USB VFS   ", 11);            // volume label (11B)
  memcpy(b + 54, "FAT12   ", 8);               // fs type (8B)
  b[510] = 0x55; b[511] = 0xAA;
}

static void buildFAT() {
  uint8_t *fat = disk[1];
  memset(fat, 0, DISK_SECTOR_SIZE);
  fat12_set(fat, 0, 0xFF8);
  fat12_set(fat, 1, 0xFFF);
  // 簇 2 空闲 (0x000): 无文件指向数据区第一个簇
  for (uint16_t c = STATUS_FIRST_CLUST; c < STATUS_FIRST_CLUST + STATUS_CLUSTERS - 1; c++)
    fat12_set(fat, c, c + 1);                            // STATUS 链 3→4→…→9→10
  fat12_set(fat, STATUS_FIRST_CLUST + STATUS_CLUSTERS - 1, 0xFFF);  // STATUS 链尾
  fat12_set(fat, README_CLUSTER, 0xFFF);                 // README 单簇文件 → EOC
  // 簇 12-62 空闲 0x000
}

static void dirEntry(uint8_t *e, const char *name8, const char *ext3,
                     uint8_t attr, uint16_t cluster, uint32_t size) {
  memset(e, 0, 32);
  memset(e, ' ', 8);                                   // name 域固定 8 字节空格填充
  memcpy(e, name8, strnlen(name8, 8));
  memset(e + 8, ' ', 3);                               // ext 域固定 3 字节
  memcpy(e + 8, ext3, strnlen(ext3, 3));
  e[11] = attr;
  e[13] = FAT_MS2B(30, 0);                 // 2026-08-16 13:42:30 固定时间戳
  e[14] = FAT_HMS2B(13, 42, 30);
  e[16] = FAT_YMD2B(2026, 8, 16);
  e[18] = FAT_YMD2B(2026, 8, 16);
  e[22] = FAT_HMS2B(13, 42, 30);
  e[24] = FAT_YMD2B(2026, 8, 16);
  e[26] = FAT_U8(cluster); e[27] = FAT_U8(cluster >> 8);
  e[28] = FAT_U8(size);     e[29] = FAT_U8(size >> 8);
  e[30] = FAT_U8(size >> 16); e[31] = FAT_U8(size >> 24);
}

static void buildRootDir() {
  uint8_t *root = disk[2];
  memset(root, 0, DISK_SECTOR_SIZE);
  dirEntry(root + 0,  "USB VFS", "", 0x08, 0, 0);                              // 卷标
  dirEntry(root + 32, "README", "TXT", 0x20, README_CLUSTER, sizeof(README_CONTENTS) - 1);
  dirEntry(root + 64, "STATUS", "TXT", 0x20, STATUS_FIRST_CLUST, 0);           // size 动态刷新
}

// ============================== STATUS.TXT 动态刷新 ==============================
static constexpr size_t   STATUS_BUF_SIZE = 256;
static char               statusBuf[STATUS_BUF_SIZE];

// MOCK 快照: 500ms tick 变化的假数据。真机替换为 w96p::Snapshot 字段。
static void buildStatusText() {
  uint32_t t = millis() / 1000;
  int  speed = (t / 5) % 101;                // 0-100 循环
  int  gear  = (t / 3) % 4 + 1;              // 1-4
  bool on    = (t / 7) % 2 == 0;
  float bat  = 4.05f - 0.05f * ((t / 11) % 10);  // 假电压
  int  mot   = on ? (120 + speed * 3) : 0;
  snprintf(statusBuf, sizeof(statusBuf),
           "W96P USB VFS MOCK\r\n"
           "SPD  %3d%%\r\n"
           "GEAR %d\r\n"
           "POW  %s\r\n"
           "BAT  %.2fV\r\n"
           "MOT  %dmA\r\n"
           "TUR  0s\r\n"
           "TS   %u\r\n",
           speed, gear, on ? "ON" : "OFF", bat, mot, (unsigned)t);
}

// 把文本刷进 STATUS 固定簇(扇区 STATUS_SECTOR0 起), 不足补空格; 更新目录项大小。
static void refreshStatusFile() {
  size_t len = strlen(statusBuf);
  if (len > STATUS_CAPACITY) len = STATUS_CAPACITY;
  for (int i = 0; i < STATUS_CLUSTERS; i++) {
    uint8_t *sec = disk[STATUS_SECTOR0 + i];
    size_t off = (size_t)i * DISK_SECTOR_SIZE;
    if (off < len) {
      size_t chunk = (len - off < DISK_SECTOR_SIZE) ? (len - off) : DISK_SECTOR_SIZE;
      memcpy(sec, statusBuf + off, chunk);
      memset(sec + chunk, 0x20, DISK_SECTOR_SIZE - chunk);   // 尾部补空格
    } else {
      memset(sec, 0x20, DISK_SECTOR_SIZE);
    }
  }
  uint8_t *dir = disk[2] + 64;                               // 目录项2 = STATUS.TXT
  uint32_t sz  = (uint32_t)len;
  dir[28] = FAT_U8(sz); dir[29] = FAT_U8(sz >> 8);
  dir[30] = FAT_U8(sz >> 16); dir[31] = FAT_U8(sz >> 24);
}

// ============================== MSC 回调 ==============================
static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  if (lba >= DISK_SECTOR_COUNT || offset + bufsize > DISK_SECTOR_SIZE) return 0;
  memcpy(buffer, disk[lba] + offset, bufsize);
  return bufsize;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  // 只读盘: SCSI 层已拒绝, 此回调不应被调用; 触发则打印(便于发现异常)
  Serial.printf("MSC WRITE (READ-ONLY, rejected): lba=%" PRIu32 " off=%" PRIu32 " n=%" PRIu32 "\n",
                lba, offset, bufsize);
  (void)buffer;
  return bufsize;   // 不落盘
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
  Serial.printf("MSC start/stop: power=%u start=%d eject=%d\n",
                power_condition, start, load_eject);
  return true;
}

static void usbEventCallback(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == ARDUINO_USB_EVENTS) {
    switch (event_id) {
      case ARDUINO_USB_STARTED_EVENT: Serial.println("[usb] PLUGGED"); break;
      case ARDUINO_USB_STOPPED_EVENT: Serial.println("[usb] UNPLUGGED"); break;
      case ARDUINO_USB_SUSPEND_EVENT: Serial.println("[usb] SUSPENDED"); break;
      case ARDUINO_USB_RESUME_EVENT:  Serial.println("[usb] RESUMED"); break;
      default: break;
    }
  }
}

// ============================== 主程序 ==============================
static void buildImage() {
  buildBootSector();
  buildFAT();
  buildRootDir();
  memcpy(disk[README_SECTOR], README_CONTENTS, sizeof(README_CONTENTS) - 1);  // README 内容
  buildStatusText();
  refreshStatusFile();
  Serial.printf("[vfs] image built: %u sectors x %u B, STATUS.TXT capacity %u B\n",
                DISK_SECTOR_COUNT, DISK_SECTOR_SIZE, STATUS_CAPACITY);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  buildImage();

  USB.onEvent(usbEventCallback);
  MSC.vendorID("M5STK");           // max 8
  MSC.productID("W96P VFS");       // max 16
  MSC.productRevision("1.0");      // max 4
  MSC.onStartStop(onStartStop);
  MSC.onRead(onRead);
  MSC.onWrite(onWrite);
  MSC.mediaPresent(true);
  MSC.isWritable(false);           // 只读盘
  MSC.begin(DISK_SECTOR_COUNT, DISK_SECTOR_SIZE);
  USB.begin();

  Serial.println("[vfs] USB VFS ready - plug USB-C into PC");
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 500) {
    last = millis();
    buildStatusText();
    refreshStatusFile();
    Serial.printf("[vfs] STATUS.TXT refreshed (%u bytes)\n", (unsigned)strlen(statusBuf));
  }
}
