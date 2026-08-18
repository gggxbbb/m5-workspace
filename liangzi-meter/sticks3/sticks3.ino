// liangzi-meter — StickS3 固件
// 通过原生 USB CDC 串口接收上位机配置（WiFi/NTP/API key/峰谷时段覆盖），
// 显示北京时间、峰谷状态（高峰期「梁文峰」/ 非高峰期「梁文谷」）与 DeepSeek 账户余额。
//
// 协议：JSON Lines @115200（见 docs/adr/0002-jsonl-serial-protocol.md）
//   上位机→固件: ping / config / get_state
//   固件→上位机: hello / ack / state

#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include "config.h"
#include "certs.h"
#include "peak_font.h"

#define FW_VERSION "1.0.0"
#define MODEL_NAME "StickS3"

#define BALANCE_HOST "api.deepseek.com"
#define BALANCE_PATH "/user/balance"
#define BALANCE_INTERVAL_MS (5UL * 60UL * 1000UL)  // 余额刷新周期 5 分钟
#define NTP_RESYNC_MS (6UL * 3600UL * 1000UL)      // NTP 每 6 小时重同步
#define STATE_INTERVAL_MS (60UL * 1000UL)          // 状态上报周期 60 秒
#define SERIAL_BUF 512

// ---------- 全局状态 ----------

Config cfg;
Preferences prefs;

bool timeSynced = false;
int  page = 0;            // 0=主页面 1=详情页
bool fullRedraw = true;

// 余额状态
double balance = 0.0;
bool   balanceValid = false;
bool   balanceExpired = false;
unsigned long lastBalanceFetch = 0;
bool   balanceForce = true;  // 开机/下发配置/按 KEY1 时置位，WiFi 就绪后立即查询
bool   fetching = false;

unsigned long lastWifiAttempt = 0;
unsigned long lastNtpAttempt = 0;
unsigned long lastStateSend = 0;
unsigned long lastDisplayTick = 0;

char serialBuf[SERIAL_BUF];
size_t serialLen = 0;

// ---------- 串口协议：发送 ----------

static void sendJson(JsonDocument &doc) {
  serializeJson(doc, Serial);
  Serial.println();
}

void sendHello() {
  JsonDocument doc;
  doc["type"] = "hello";
  doc["fw"] = FW_VERSION;
  doc["model"] = MODEL_NAME;
  doc["configured"] = cfg.hasWifi();
  sendJson(doc);
}

void sendAck(bool ok, const char *msg) {
  JsonDocument doc;
  doc["type"] = "ack";
  doc["ok"] = ok;
  doc["msg"] = msg;
  sendJson(doc);
}

void sendState() {
  time_t now = time(nullptr);
  JsonDocument doc;
  doc["type"] = "state";
  doc["ts"] = now;
  struct tm tmv;
  localtime_r(&now, &tmv);
  char tbuf[32];
  snprintf(tbuf, sizeof(tbuf), "%04d-%02d-%02d %02d:%02d:%02d",
           tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
           tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  doc["time"] = tbuf;
  int md = tmv.tm_hour * 60 + tmv.tm_min;
  if (timeSynced) {
    doc["phase"] = inPeakWindow(cfg, md) ? "peak" : "offpeak";
  } else {
    doc["phase"] = "unknown";
  }
  doc["balance"] = balanceValid ? balance : -1;
  doc["balance_valid"] = balanceValid;
  doc["balance_expired"] = balanceExpired;
  doc["wifi"] = WiFi.status() == WL_CONNECTED;
  doc["ssid"] = cfg.ssid;
  doc["ip"] = WiFi.localIP().toString();
  doc["battery"] = M5.Power.getBatteryLevel();
  doc["fw"] = FW_VERSION;
  sendJson(doc);
}

// ---------- 串口协议：接收 ----------

static int hhmmToMin(const char *s) {
  if (strlen(s) != 5 || s[2] != ':') return -1;
  int h = atoi(s);
  int m = atoi(s + 3);
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

void applyConfig() {
  // 若 wifi 参数变化或当前未连接则重连
  WiFi.disconnect();
  if (cfg.hasWifi()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid.c_str(), cfg.password.length() ? cfg.password.c_str() : NULL);
  } else {
    WiFi.mode(WIFI_OFF);
  }
  lastWifiAttempt = millis();
  // NTP 立即用新服务器重新同步
  configTime(8 * 3600, 0, cfg.ntp.c_str());
  lastNtpAttempt = millis();
  // API key 变化后立即重查余额
  balanceForce = true;
  balanceValid = false;
  balanceExpired = false;
  fullRedraw = true;
}

void handleConfig(JsonDocument &doc) {
  JsonObject wifi = doc["wifi"];
  if (!wifi.isNull()) {
    cfg.ssid = wifi["ssid"] | "";
    cfg.password = wifi["password"] | "";
  }
  cfg.ntp = doc["ntp"] | DEFAULT_NTP;
  cfg.apiKey = doc["api_key"] | "";
  cfg.balanceWarn = doc["balance_warn"] | DEFAULT_BALANCE_WARN;

  JsonArray ranges = doc["peak_ranges"];
  if (ranges.isNull()) {
    cfg.peakCount = 0;  // 回退官方默认
  } else {
    int n = 0;
    for (JsonVariant v : ranges) {
      if (n >= MAX_PEAK_RANGES) break;
      const char *s = v["start"] | "";
      const char *e = v["end"] | "";
      int sm = hhmmToMin(s);
      int em = hhmmToMin(e);
      if (sm < 0 || em < 0) continue;
      cfg.peak[n].startMin = sm;
      cfg.peak[n].endMin = em;
      n++;
    }
    cfg.peakCount = n;
  }

  cfgSave(cfg, prefs);
  applyConfig();
  sendAck(true, "config applied");
}

void handleLine(char *line) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) {
    sendAck(false, "bad json");
    return;
  }
  const char *type = doc["type"] | "";
  if (strcmp(type, "ping") == 0) {
    sendHello();
  } else if (strcmp(type, "config") == 0) {
    handleConfig(doc);
  } else if (strcmp(type, "get_state") == 0) {
    sendState();
  } else {
    // 未知 type：忽略，保证协议向前兼容
  }
}

void pumpSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n') {
      serialBuf[serialLen] = 0;
      if (serialLen > 0) handleLine(serialBuf);
      serialLen = 0;
    } else if (ch != '\r' && serialLen < SERIAL_BUF - 1) {
      serialBuf[serialLen++] = ch;
    }
  }
}

// ---------- 网络 ----------

void manageWiFi() {
  if (!cfg.hasWifi()) return;
  if (WiFi.status() != WL_CONNECTED &&
      millis() - lastWifiAttempt > 30000UL) {
    lastWifiAttempt = millis();
    WiFi.begin(cfg.ssid.c_str(), cfg.password.length() ? cfg.password.c_str() : NULL);
  }
}

void manageNtp() {
  if (WiFi.status() != WL_CONNECTED) return;
  unsigned long interval = timeSynced ? NTP_RESYNC_MS : 30000UL;
  if (millis() - lastNtpAttempt > interval) {
    lastNtpAttempt = millis();
    configTime(8 * 3600, 0, cfg.ntp.c_str());
  }
}

bool fetchBalance(double &out, bool &available) {
  WiFiClientSecure client;
  client.setCACert(ROOT_CA);
  client.setTimeout(6);
  if (!client.connect(BALANCE_HOST, 443)) return false;

  client.print("GET " BALANCE_PATH " HTTP/1.1\r\n");
  client.print("Host: " BALANCE_HOST "\r\n");
  client.print("Authorization: Bearer ");
  client.print(cfg.apiKey);
  client.print("\r\nUser-Agent: liangzi-meter/" FW_VERSION "\r\nConnection: close\r\n\r\n");

  // 字节级读取：头与 body 一并累积，300ms 无数据判定响应结束
  String resp;
  uint32_t lastData = millis();
  while (millis() - lastData < 300 && resp.length() < 4096) {
    while (client.available()) {
      int c = client.read();
      if (c < 0) continue;
      resp += (char)c;
      lastData = millis();
    }
    delay(10);
  }

  int split = resp.indexOf("\r\n\r\n");
  if (split < 0) return false;
  String head = resp.substring(0, split);
  String body = resp.substring(split + 4);

  int sp = head.indexOf(' ');
  int status = (sp >= 0) ? atoi(head.c_str() + sp + 1) : 0;
  if (status != 200) return false;

  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  if (!doc["is_available"].is<bool>()) return false;
  available = doc["is_available"] | false;
  JsonArray infos = doc["balance_infos"].as<JsonArray>();
  if (infos.isNull() || infos.size() == 0) return false;
  const char *tb = infos[0]["total_balance"] | "";
  if (!tb || !*tb) return false;
  out = strtod(tb, nullptr);
  return true;
}

void manageBalance() {
  if (fetching) return;
  bool due = cfg.apiKey.length() > 0 &&
             WiFi.status() == WL_CONNECTED &&
             (balanceForce || millis() - lastBalanceFetch > BALANCE_INTERVAL_MS);
  if (!due) return;
  fetching = true;
  bool available = true;
  bool ok = fetchBalance(balance, available);
  if (ok) {
    balanceValid = true;
    balanceExpired = false;
  } else {
    balanceExpired = balanceValid;  // 保留旧值并标记过期
  }
  (void)available;
  lastBalanceFetch = millis();
  balanceForce = false;
  fetching = false;
}

// ---------- 显示 ----------

#define CLR_BG M5.Display.color565(10, 12, 16)
#define CLR_FG M5.Display.color565(230, 234, 240)
#define CLR_DIM M5.Display.color565(120, 128, 140)
#define CLR_PEAK M5.Display.color565(255, 200, 50)   // 高峰期「梁文峰」：黄色（见 ADR-0003）
#define CLR_OFF M5.Display.color565(64, 208, 120)    // 非高峰期「梁文谷」：绿色
#define CLR_WARN M5.Display.color565(255, 70, 70)    // 余额低于阈值：红色（唯一红色用法）

const char *weekdayCn(int wday) {
  static const char *map[7] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
  return map[wday];
}

void fillLine(int y, int h) {
  M5.Display.fillRect(0, y, M5.Display.width(), h, CLR_BG);
}

void drawTopLine(const char *s, int y, uint16_t color) {
  fillLine(y - 8, 16);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(color);
  M5.Display.drawString(s, M5.Display.width() / 2, y);
}

void renderMain(bool synced, time_t now) {
  struct tm tmv;
  localtime_r(&now, &tmv);
  int md = tmv.tm_hour * 60 + tmv.tm_min;
  bool peak = synced && inPeakWindow(cfg, md);

  // 顶部：北京时间
  char tbuf[24];
  if (synced) {
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d %02d-%02d %s", tmv.tm_hour, tmv.tm_min,
             tmv.tm_sec, tmv.tm_mon + 1, tmv.tm_mday, weekdayCn(tmv.tm_wday));
  } else {
    snprintf(tbuf, sizeof(tbuf), "--:--:-- 等待同步");
  }
  fillLine(4, 16);
  M5.Display.setFont(&fonts::efontCN_12);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(CLR_DIM);
  M5.Display.drawString(tbuf, M5.Display.width() / 2, 14);

  // 当前时段剩余（标签）
  fillLine(24, 16);
  M5.Display.setFont(&fonts::efontCN_12);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(CLR_DIM);
  M5.Display.drawString("当前时段剩余", M5.Display.width() / 2, 31);

  // 当前时段剩余（倒计时数字）
  fillLine(38, 20);
  M5.Display.setFont(&fonts::efontCN_16);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(CLR_FG);
  char cbuf[16];
  if (synced) {
    long secs = secondsToNextSwitch(cfg, now, md);
    snprintf(cbuf, sizeof(cbuf), "%02ld:%02ld:%02ld", secs / 3600, (secs / 60) % 60, secs % 60);
  } else {
    strcpy(cbuf, "--:--:--");
  }
  M5.Display.drawString(cbuf, M5.Display.width() / 2, 47);

  // 小字：现在是
  fillLine(56, 16);
  M5.Display.setFont(&fonts::efontCN_12);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(CLR_DIM);
  M5.Display.drawString("现在是", M5.Display.width() / 2, 63);

  // 超大字：梁文谷（绿）/ 梁文峰（黄）——预渲染抗锯齿位图（见 peak_font.h），垂直居中
  fillLine(96, 48);
  if (!synced) {
    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextColor(CLR_DIM);
    M5.Display.drawString("时间未同步", M5.Display.width() / 2, 120);
  } else {
    const uint16_t *img = peak ? PEAK_WENFENG : PEAK_WENGU;
    M5.Display.pushImage((M5.Display.width() - PEAK_W) / 2, 120 - PEAK_H / 2, PEAK_W, PEAK_H, img);
  }

  // 小字：余额
  fillLine(162, 16);
  M5.Display.setFont(&fonts::efontCN_12);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(CLR_DIM);
  M5.Display.drawString("余额", M5.Display.width() / 2, 170);

  // 较大字：金额（低于阈值红色）
  fillLine(184, 28);
  M5.Display.setFont(&fonts::efontCN_24);
  M5.Display.setTextDatum(middle_center);
  char bbuf[32];
  if (!balanceValid) {
    snprintf(bbuf, sizeof(bbuf), "--");
    M5.Display.setTextColor(CLR_DIM);
  } else {
    snprintf(bbuf, sizeof(bbuf), "%.2f元", balance);
    if (balance < cfg.balanceWarn) {
      M5.Display.setTextColor(CLR_WARN);  // 红色告警
    } else {
      M5.Display.setTextColor(CLR_FG);
    }
  }
  M5.Display.drawString(bbuf, M5.Display.width() / 2, 200);

  // 底部提示
  fillLine(220, 16);
  M5.Display.setFont(&fonts::efontCN_12);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(CLR_DIM);
  M5.Display.drawString("KEY1 刷新  KEY2 详情", M5.Display.width() / 2, 228);
}

void renderInfo(bool synced) {
  (void)synced;
  // 余额大字
  M5.Display.setFont(&fonts::efontCN_12);
  drawTopLine("余额详情", 10, CLR_DIM);
  fillLine(20, 40);
  M5.Display.setFont(&fonts::efontCN_24);
  M5.Display.setTextDatum(middle_center);
  char bbuf[32];
  if (!balanceValid) {
    snprintf(bbuf, sizeof(bbuf), "--");
    M5.Display.setTextColor(CLR_DIM);
  } else {
    snprintf(bbuf, sizeof(bbuf), "%.2f 元", balance);
    M5.Display.setTextColor(CLR_FG);
  }
  M5.Display.drawString(bbuf, M5.Display.width() / 2, 46);

  // 余额状态
  fillLine(64, 18);
  M5.Display.setFont(&fonts::efontCN_12);
  M5.Display.setTextDatum(middle_center);
  if (!cfg.apiKey.length()) {
    M5.Display.setTextColor(CLR_WARN);
    M5.Display.drawString("未配置 API Key", M5.Display.width() / 2, 74);
  } else if (balanceValid && balanceExpired) {
    M5.Display.setTextColor(CLR_WARN);
    M5.Display.drawString("查询失败 · 显示旧数据", M5.Display.width() / 2, 74);
  } else if (balanceValid) {
    M5.Display.setTextColor(CLR_OFF);
    M5.Display.drawString("查询正常", M5.Display.width() / 2, 74);
  } else {
    M5.Display.setTextColor(CLR_WARN);
    M5.Display.drawString("等待首次查询", M5.Display.width() / 2, 74);
  }

  // 详情行
  int y = 96;
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(CLR_DIM);
  char line[64];
  snprintf(line, sizeof(line), "WiFi  %s", cfg.hasWifi() ? cfg.ssid.c_str() : "未配置");
  M5.Display.drawString(line, 10, y); y += 18;
  snprintf(line, sizeof(line), "IP    %s", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "-");
  M5.Display.drawString(line, 10, y); y += 18;
  snprintf(line, sizeof(line), "NTP   %s", cfg.ntp.c_str());
  M5.Display.drawString(line, 10, y); y += 18;
  snprintf(line, sizeof(line), "时段  %s", cfg.peakCount > 0 ? "自定义" : "官方默认");
  M5.Display.drawString(line, 10, y); y += 18;
  snprintf(line, sizeof(line), "固件  v%s · %s", FW_VERSION, MODEL_NAME);
  M5.Display.drawString(line, 10, y); y += 18;
  int batt = M5.Power.getBatteryLevel();
  snprintf(line, sizeof(line), "电量  %d%%", batt < 0 ? 0 : batt);
  M5.Display.drawString(line, 10, y); y += 18;

  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(CLR_DIM);
  M5.Display.drawString("KEY2 返回", M5.Display.width() / 2, 228);
}

void updateDisplay() {
  time_t now = time(nullptr);
  bool synced = now > 1600000000L;
  if (synced != timeSynced) {
    timeSynced = synced;
    fullRedraw = true;
  }
  if (fullRedraw) {
    M5.Display.fillScreen(CLR_BG);
    fullRedraw = false;
    if (page == 0) {
      renderMain(synced, now);
    } else {
      renderInfo(synced);
    }
  } else if (page == 0) {
    renderMain(synced, now);
  } else {
    renderInfo(synced);
  }
}

// ---------- 按键 ----------

void handleKeys() {
  if (M5.BtnA.wasClicked()) {  // KEY1：立即刷新余额
    if (cfg.apiKey.length()) {
      balanceForce = true;
      balanceValid = false;
    }
  }
  if (M5.BtnB.wasClicked()) {  // KEY2：切换页面
    page = 1 - page;
    fullRedraw = true;
  }
}

// ---------- 主流程 ----------

void setup() {
  auto m5cfg = M5.config();
  M5.begin(m5cfg);
  M5.Display.setRotation(0);  // 竖屏 135x240
  M5.Display.setBrightness(160);

  Serial.begin(115200);
  delay(300);

  cfgLoad(cfg, prefs);

  if (cfg.hasWifi()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid.c_str(), cfg.password.length() ? cfg.password.c_str() : NULL);
  } else {
    WiFi.mode(WIFI_OFF);
  }
  configTime(8 * 3600, 0, cfg.ntp.c_str());
  lastWifiAttempt = lastNtpAttempt = millis();

  sendHello();
}

void loop() {
  M5.update();
  handleKeys();
  pumpSerial();
  manageWiFi();
  manageNtp();
  manageBalance();
  if (millis() - lastStateSend > STATE_INTERVAL_MS) {
    lastStateSend = millis();
    sendState();
  }
  if (millis() - lastDisplayTick > 500) {  // 显示刷新节流 500ms
    lastDisplayTick = millis();
    updateDisplay();
  }
}
