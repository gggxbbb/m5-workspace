// ESP-NOW 冒烟测试：初始化 + 注册对端 + 收发回调
#include <WiFi.h>
#include <esp_now.h>

uint8_t peerMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // 广播占位

void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {}
void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);           // ESP-NOW 要求 STA 模式
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init FAILED");
    while (true) delay(100);
  }
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, peerMac, 6);
  peer.channel = 0;              // 0 = 跟随当前信道
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  Serial.print("ESP-NOW ready, MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  const char msg[] = "ping";
  esp_now_send(peerMac, (const uint8_t *)msg, sizeof(msg));
  delay(1000);
}
