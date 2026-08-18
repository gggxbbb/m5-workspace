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
bool fullRedraw = true;   // 强制重绘（页面切换/配置变更/同步态翻转时置位）

// 双缓冲帧缓冲：所有绘制先画到 canvas，完成后 pushSprite 一次性推屏幕，
// 消除「擦除→绘制」之间的闪屏。StickS3 有 8MB OPI PSRAM，canvas 置于 PSRAM。
M5Canvas canvas(&M5.Display);

// 峰谷大字预渲染 sprite：开机时从稀疏像素表（peak_font.h）drawPixel 填入，
// 之后不变。显示时 peakSprite[idx].pushSprite(&canvas, ox, oy) 一次内存拷贝，
// 替代每次 2500+ 次 drawPixel 循环。背景=CLR_BG，覆盖主 canvas 同色背景无视觉差异。
// [0]=梁文峰(黄)  [1]=梁文谷(绿)
M5Canvas peakSprite[2] = { M5Canvas(&M5.Display), M5Canvas(&M5.Display) };

// 帧内容快照：只在内容真正变化时才重绘 + pushSprite，避免无谓刷新导致发热。
struct FrameSnap {
  char    time[24];       // 顶部时间串
  char    countdown[16];  // 倒计时串
  char    balance[32];    // 余额串
  uint8_t peakImg;        // 0=未同步 1=峰 2=谷
  uint8_t page;           // 当前页码
  bool    lowBalance;     // 余额告警态
};
FrameSnap prevSnap;           // 零初始化
bool      snapValid = false;  // 首帧必触发全绘

// 余额状态
double balance = 0.0;
bool   balanceValid = false;
bool   balanceExpired = false;
unsigned long lastBalanceFetch = 0;
bool   balanceForce = true;  // 开机/下发配置/按 KEY1 时置位，WiFi 就绪后立即查询

// 余额查询状态机（异步）——避免同步阻塞导致 loop 卡死、显示刷新停顿（黑屏诱因）。
// connect 仍同步（TLS 握手 1-2s，受 setTimeout 控制），但读取阶段非阻塞：
// 每次 loop 只读当前 available 的字节，查询期间 loop 继续转，显示/串口/按键不中断。
enum BalanceState {
  BS_IDLE,
  BS_CONNECTING,  // TCP+TLS 握手
  BS_SENDING,     // 发送 HTTP 请求
  BS_READING,     // 非阻塞读响应，累积到 bsResp
  BS_PARSING,     // 解析并更新全局 balance
};
BalanceState    bsState = BS_IDLE;
WiFiClientSecure bsClient;   // 跨状态保持，IDLE 时空闲
String          bsResp;
uint32_t        bsLastData;        // READ 阶段最后收到数据时间（静默超时用）
uint32_t        bsReadStart;       // READ 阶段开始时间（整体超时保底用）
double          bsResultBalance;
bool            bsResultAvailable;
#define BS_READ_SILENCE_MS  300    // 读响应静默超时：300ms 无新数据判定结束
#define BS_READ_TOTAL_MS    5000   // 读响应整体超时保底

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
  if (bsState != BS_IDLE) {
    // 中断正在进行的查询（可能在用旧 key），丢弃结果，回 IDLE 让新配置立即重查
    bsClient.stop();
    bsResp = "";
    bsState = BS_IDLE;
  }
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

// 完成一次查询（成功或失败），更新全局余额状态，清理连接，回 IDLE
static void finishBalance(bool ok) {
  if (ok) {
    balance = bsResultBalance;
    balanceValid = true;
    balanceExpired = false;
  } else {
    balanceExpired = balanceValid;  // 保留旧值并标记过期
  }
  lastBalanceFetch = millis();
  balanceForce = false;
  bsClient.stop();
  bsResp = "";
  bsState = BS_IDLE;
}

// 解析累积的完整 HTTP 响应（bsResp 含头+body），写入 bsResult*
static bool parseBalanceResponse() {
  int split = bsResp.indexOf("\r\n\r\n");
  if (split < 0) return false;
  String head = bsResp.substring(0, split);
  String body = bsResp.substring(split + 4);

  int sp = head.indexOf(' ');
  int status = (sp >= 0) ? atoi(head.c_str() + sp + 1) : 0;
  if (status != 200) return false;

  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  if (!doc["is_available"].is<bool>()) return false;
  bsResultAvailable = doc["is_available"] | false;
  JsonArray infos = doc["balance_infos"].as<JsonArray>();
  if (infos.isNull() || infos.size() == 0) return false;
  const char *tb = infos[0]["total_balance"] | "";
  if (!tb || !*tb) return false;
  bsResultBalance = strtod(tb, nullptr);
  return true;
}

void manageBalance() {
  // IDLE：检查是否该发起查询
  if (bsState == BS_IDLE) {
    bool due = cfg.apiKey.length() > 0 &&
               WiFi.status() == WL_CONNECTED &&
               (balanceForce || millis() - lastBalanceFetch > BALANCE_INTERVAL_MS);
    if (!due) return;
    bsResp = "";
    bsResultAvailable = true;
    bsClient.setCACert(ROOT_CA);
    bsClient.setTimeout(6);  // connect/TLS 握手超时（秒）
    bsState = BS_CONNECTING;
    // 落到下面执行 connect
  }

  switch (bsState) {
    case BS_CONNECTING: {
      // connect 同步（TLS 握手 1-2s）；失败或超时立即结束
      if (bsClient.connect(BALANCE_HOST, 443)) {
        bsState = BS_SENDING;
      } else {
        finishBalance(false);
      }
      break;
    }
    case BS_SENDING: {
      bsClient.print("GET " BALANCE_PATH " HTTP/1.1\r\n");
      bsClient.print("Host: " BALANCE_HOST "\r\n");
      bsClient.print("Authorization: Bearer ");
      bsClient.print(cfg.apiKey);
      bsClient.print("\r\nUser-Agent: liangzi-meter/" FW_VERSION "\r\nConnection: close\r\n\r\n");
      bsState = BS_READING;
      bsReadStart = millis();
      bsLastData = millis();
      break;
    }
    case BS_READING: {
      // 非阻塞读：每次 loop 只读当前 available 的字节，不 delay、不阻塞
      while (bsClient.available() && bsResp.length() < 4096) {
        int c = bsClient.read();
        if (c < 0) break;
        bsResp += (char)c;
        bsLastData = millis();
      }
      // 结束判定：连接关闭且无残留 / 静默超时 / 整体超时 / 缓冲区满
      if (!bsClient.connected() && !bsClient.available()) {
        bsState = BS_PARSING;
      } else if (millis() - bsLastData > BS_READ_SILENCE_MS) {
        bsState = BS_PARSING;
      } else if (millis() - bsReadStart > BS_READ_TOTAL_MS) {
        finishBalance(false);
      } else if (bsResp.length() >= 4096) {
        bsState = BS_PARSING;
      }
      break;
    }
    case BS_PARSING: {
      (void)bsResultAvailable;  // is_available 当前未用于显示，保留解析
      finishBalance(parseBalanceResponse());
      break;
    }
    default:
      bsState = BS_IDLE;
      break;
  }
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

// 所有绘制目标改为 canvas（双缓冲），字符串内容由 updateDisplay 预先算好存入 cur，
// 避免渲染函数内重复 snprintf。canvas 已由 updateDisplay 调用 fillScreen 清空。

void renderMainToCanvas(bool synced, time_t now, const FrameSnap &cur) {
  (void)now;
  (void)synced;  // cur.peakImg 已编码同步态

  // 顶部：北京时间
  canvas.setFont(&fonts::efontCN_12);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(CLR_DIM);
  canvas.drawString(cur.time, canvas.width() / 2, 14);

  // 「当前时段剩余」标签
  canvas.drawString("当前时段剩余", canvas.width() / 2, 31);

  // 倒计时数字
  canvas.setFont(&fonts::efontCN_16);
  canvas.setTextColor(CLR_FG);
  canvas.drawString(cur.countdown, canvas.width() / 2, 47);

  // 「现在是」
  canvas.setFont(&fonts::efontCN_12);
  canvas.setTextColor(CLR_DIM);
  canvas.drawString("现在是", canvas.width() / 2, 63);

  // 峰谷大字：预渲染 sprite 一次内存拷贝推到主 canvas（替代 drawPixel 循环）
  if (cur.peakImg == 0) {
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(CLR_DIM);
    canvas.drawString("时间未同步", canvas.width() / 2, 120);
  } else {
    int ox = (canvas.width() - PEAK_W) / 2;
    int oy = 120 - PEAK_H / 2;
    peakSprite[cur.peakImg - 1].pushSprite(&canvas, ox, oy);
  }

  // 「余额」标签
  canvas.setFont(&fonts::efontCN_12);
  canvas.setTextColor(CLR_DIM);
  canvas.drawString("余额", canvas.width() / 2, 170);

  // 金额（低于阈值红色 — 红色仅用于余额告警）
  canvas.setFont(&fonts::efontCN_24);
  canvas.setTextColor(cur.lowBalance ? CLR_WARN : CLR_FG);
  canvas.drawString(cur.balance, canvas.width() / 2, 200);

  // 底部提示
  canvas.setFont(&fonts::efontCN_12);
  canvas.setTextColor(CLR_DIM);
  canvas.drawString("KEY1 刷新  KEY2 详情", canvas.width() / 2, 228);
}

void renderInfoToCanvas(const FrameSnap &cur) {
  // canvas 已由 updateDisplay 调用 fillScreen 清空

  // 标题
  canvas.setFont(&fonts::efontCN_12);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(CLR_DIM);
  canvas.drawString("余额详情", canvas.width() / 2, 10);

  // 余额大字
  canvas.setFont(&fonts::efontCN_24);
  char bbuf[32];
  if (!balanceValid) {
    snprintf(bbuf, sizeof(bbuf), "--");
    canvas.setTextColor(CLR_DIM);
  } else {
    snprintf(bbuf, sizeof(bbuf), "%.2f 元", balance);
    canvas.setTextColor(cur.lowBalance ? CLR_WARN : CLR_FG);
  }
  canvas.drawString(bbuf, canvas.width() / 2, 46);

  // 余额状态
  canvas.setFont(&fonts::efontCN_12);
  if (!cfg.apiKey.length()) {
    canvas.setTextColor(CLR_WARN);
    canvas.drawString("未配置 API Key", canvas.width() / 2, 74);
  } else if (balanceValid && balanceExpired) {
    canvas.setTextColor(CLR_WARN);
    canvas.drawString("查询失败 · 显示旧数据", canvas.width() / 2, 74);
  } else if (balanceValid) {
    canvas.setTextColor(CLR_OFF);
    canvas.drawString("查询正常", canvas.width() / 2, 74);
  } else {
    canvas.setTextColor(CLR_WARN);
    canvas.drawString("等待首次查询", canvas.width() / 2, 74);
  }

  // 详情行
  int y = 96;
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(CLR_DIM);
  char line[64];
  snprintf(line, sizeof(line), "WiFi  %s", cfg.hasWifi() ? cfg.ssid.c_str() : "未配置");
  canvas.drawString(line, 10, y); y += 18;
  snprintf(line, sizeof(line), "IP    %s", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "-");
  canvas.drawString(line, 10, y); y += 18;
  snprintf(line, sizeof(line), "NTP   %s", cfg.ntp.c_str());
  canvas.drawString(line, 10, y); y += 18;
  snprintf(line, sizeof(line), "时段  %s", cfg.peakCount > 0 ? "自定义" : "官方默认");
  canvas.drawString(line, 10, y); y += 18;
  snprintf(line, sizeof(line), "固件  v%s · %s", FW_VERSION, MODEL_NAME);
  canvas.drawString(line, 10, y); y += 18;
  int batt = M5.Power.getBatteryLevel();
  snprintf(line, sizeof(line), "电量  %d%%", batt < 0 ? 0 : batt);
  canvas.drawString(line, 10, y); y += 18;

  canvas.setTextDatum(middle_center);
  canvas.setTextColor(CLR_DIM);
  canvas.drawString("KEY2 返回", canvas.width() / 2, 228);
}

void updateDisplay() {
  time_t now = time(nullptr);
  bool synced = now > 1600000000L;
  if (synced != timeSynced) {
    timeSynced = synced;
    fullRedraw = true;
  }

  // 计算当前帧内容快照（字符串预先算好，渲染函数直接用，避免重复 snprintf）
  struct tm tmv;
  localtime_r(&now, &tmv);
  int md = tmv.tm_hour * 60 + tmv.tm_min;
  bool peak = synced && inPeakWindow(cfg, md);

  FrameSnap cur;
  memset(&cur, 0, sizeof(cur));
  if (synced) {
    snprintf(cur.time, sizeof(cur.time), "%02d:%02d:%02d %02d-%02d %s",
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec, tmv.tm_mon + 1, tmv.tm_mday,
             weekdayCn(tmv.tm_wday));
    long secs = secondsToNextSwitch(cfg, now, md);
    if (secs < 0) secs = 0;
    snprintf(cur.countdown, sizeof(cur.countdown), "%02ld:%02ld:%02ld",
             secs / 3600, (secs / 60) % 60, secs % 60);
    cur.peakImg = peak ? 1 : 2;
  } else {
    snprintf(cur.time, sizeof(cur.time), "--:--:-- 等待同步");
    snprintf(cur.countdown, sizeof(cur.countdown), "--:--:--");
    cur.peakImg = 0;
  }
  if (!balanceValid) {
    snprintf(cur.balance, sizeof(cur.balance), "--");
    cur.lowBalance = false;
  } else {
    snprintf(cur.balance, sizeof(cur.balance), "%.2f元", balance);
    cur.lowBalance = (balance < (double)cfg.balanceWarn);
  }
  cur.page = (uint8_t)page;

  // diff：内容未变且无强制重绘信号时跳过整帧（消除无谓 CPU 负载 → 降发热）
  bool changed = fullRedraw || !snapValid ||
                 memcmp(&cur, &prevSnap, sizeof(FrameSnap)) != 0;
  if (!changed) return;

  // 重绘到 canvas（内存），完成后一次性推屏幕（消除「擦除→绘制」闪屏）
  canvas.fillScreen(CLR_BG);
  if (page == 0) {
    renderMainToCanvas(synced, now, cur);
  } else {
    renderInfoToCanvas(cur);
  }
  canvas.pushSprite(0, 0);

  prevSnap = cur;
  snapValid = true;
  fullRedraw = false;
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

  // 创建双缓冲帧缓冲（StickS3 有 8MB OPI PSRAM，优先用 PSRAM 存放 135×240×2=64.8KB）
  canvas.setPsram(true);
  if (!canvas.createSprite(135, 240)) {
    // PSRAM 不可用时退回内部 RAM
    canvas.setPsram(false);
    canvas.createSprite(135, 240);
  }

  // 预渲染峰谷大字 sprite（一次性，开机后不变）
  // 从稀疏像素表 drawPixel 填入，之后显示时 pushSprite 一次拷贝到主 canvas
  const struct PeakPixel *tables[2] = { PEAK_WENFENG, PEAK_WENGU };
  const int counts[2] = { PEAK_WENFENG_N, PEAK_WENGU_N };
  for (int i = 0; i < 2; i++) {
    peakSprite[i].setPsram(true);
    if (!peakSprite[i].createSprite(PEAK_W, PEAK_H)) {
      peakSprite[i].setPsram(false);
      peakSprite[i].createSprite(PEAK_W, PEAK_H);
    }
    peakSprite[i].fillScreen(CLR_BG);
    for (int j = 0; j < counts[i]; j++) {
      peakSprite[i].drawPixel(tables[i][j].x, tables[i][j].y, tables[i][j].c);
    }
  }

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
