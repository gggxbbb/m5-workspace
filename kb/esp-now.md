# ESP-NOW 无线通信（StickS3 ↔ Cardputer-Adv）

> 已验证：2026-08-03 用本机 m5stack:esp32 3.3.8 核心，同一 sketch 在 `m5stack_sticks3` 与 `m5stack_cardputer` 两个 FQBN 下均编译通过（`espnow-smoke/espnow-smoke.ino`）。

## 是什么

ESP-NOW 是乐鑫官方的**无连接**设备直连协议，跑在 2.4GHz Wi-Fi 射频上：

- **不需要路由器 / AP / 配网**，设备间按 MAC 地址直发
- 两块 ESP32-S3 天然支持（Arduino 核心内置 `esp_now.h`，零额外库）
- 单包 payload 最大 **250 字节**，适合传感器数据、遥控指令、状态同步
- 空旷环境实测距离几十米级；支持可选加密（PMK/LMK）

## 最小骨架（核心 3.x / ESP-IDF 5.x 写法）

```cpp
#include <WiFi.h>
#include <esp_now.h>

uint8_t peerMac[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}; // 对方 MAC

void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {}   // ← 注意签名
void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {}

void setup() {
  WiFi.mode(WIFI_STA);            // ESP-NOW 必须 STA 模式
  WiFi.disconnect();
  esp_now_init();
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, peerMac, 6);
  peer.channel = 0;               // 0 = 跟随当前信道
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

void loop() {
  esp_now_send(peerMac, (const uint8_t *)"ping", 5);
  delay(1000);
}
```

## 易错点

1. **回调签名新旧不通用**（核心 3.x 起变更，照抄网上旧教程必编译错）：
   - 发送回调：`void cb(const wifi_tx_info_t*, esp_now_send_status_t)`（旧版是 `const uint8_t*`）
   - 接收回调：`void cb(const esp_now_recv_info_t*, const uint8_t*, int)`（旧版三参是 `const uint8_t* mac`）
2. **双方信道必须一致**：`peer.channel = 0` 时跟随本机当前信道；若一方同时连着路由器，信道被路由器锁定，另一方要显式设成同一信道
3. **互拿 MAC**：`WiFi.macAddress()` 先各自打印出来，写死到对方代码里（或做广播发现）
4. **发广播也要 add_peer**：FF:FF:FF:FF:FF:FF 同样需要注册为 peer 才能发
5. **与 M5Unified 共存无冲突**：`M5.begin()` 之后再 `WiFi.mode(WIFI_STA)` + `esp_now_init()` 即可；但 Wi-Fi 射频开启会显著增加功耗（手持电池设备注意）
6. **250 字节上限**：超过直接报错，大包要自己分片

## 验证记录

| FQBN | 结果 | Flash 占用 |
|---|---|---|
| m5stack:esp32:m5stack_sticks3 | ✅ 编译通过 | 881831 B (26%) |
| m5stack:esp32:m5stack_cardputer | ✅ 编译通过 | 854944 B (65%) |

冒烟 sketch 保留在仓库 `espnow-smoke/`，可直接 `arduino-cli compile --fqbn <FQBN> espnow-smoke` 复验。

**为什么占用百分比差那么多**：两板固件实际大小几乎相同（StickS3 881831 B 反而大 27KB，因编入 OPI PSRAM 支持）。差异在默认分区表——`boards.txt` 中 sticks3 的 PartitionScheme 菜单首项是 `default_8MB`（app0=3.2MB），cardputer 首项是 `default`（app0=1.25MB）。Cardputer 项目超 1.25MB 时加 `--board-options PartitionScheme=default_8MB`（两板都是 8MB Flash）；不要 OTA 可用 `huge_app`。

## 参考

- 乐鑫官方文档：https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/network/esp_now.html
- Arduino 示例：核心自带 `File → Examples → ESP32 → ESPNOW`（在 Arduino IDE 中）
