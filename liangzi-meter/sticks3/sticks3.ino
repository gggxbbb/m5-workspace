// liangzi-meter — StickS3 固件 (v1.4.0 Obsidian HUD)
// 通过原生 USB CDC 串口接收上位机配置（WiFi/NTP/API key/峰谷时段覆盖/提示音/屏幕方向），
// 显示北京时间、峰谷状态（高峰期「梁文峰」/ 非高峰期「梁文谷」）、DeepSeek 余额拆分与消耗趋势。
//
// 协议：JSON Lines @115200（见 docs/adr/0002-jsonl-serial-protocol.md）
//   上位机→固件: ping / config / get_state
//   固件→上位机: hello / ack / state

#include <ArduinoJson.h>
#include <LiangziUi.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <cstring>
#include <cctype>
#include <esp_sntp.h>
#include <memory>
#include <new>
#include <string>
#include <sys/time.h>
#include <time.h>
#include "alert.h"
#include "certs.h"
#include "config.h"

#define FW_VERSION "1.4.0"
#define MODEL_NAME "StickS3"

#define BALANCE_HOST "api.deepseek.com"
#define BALANCE_PATH "/user/balance"
#define BALANCE_INTERVAL_MS (5UL * 60UL * 1000UL)  // 非高峰默认刷新周期 5 分钟
#define PEAK_QUERY_INTERVAL_MS 15000UL             // 高峰 RWR 监控周期 15 秒（ADR-0004）
#define NTP_FAST_RETRY_MS 4000UL                   // 首轮快速轮询备用源间隔 4 秒
#define NTP_SLOW_RETRY_MS 30000UL                  // 仍未同步时的退避重试周期 30 秒
#define NTP_UPGRADE_RETRY_MS 60000UL               // HTTP Date 已兜底后等待精确 NTP 的间隔 60 秒
#define NTP_SLEEP_BOOST_MS 15000UL                 // 连网后最多关闭 Wi-Fi 省电 15 秒以加速首包
#define NTP_RESYNC_MS (6UL * 3600UL * 1000UL)      // NTP 精确同步后每 6 小时重同步
#define STATE_INTERVAL_MS (60UL * 1000UL)          // 状态上报周期 60 秒
#define SERIAL_BUF 512

// ---------- 屏幕旋转（ADR-0005）----------
// M5GFX rotation: 0=竖屏正常 1=顺时针90°(横屏) 2=180° 3=逆时针90°(横屏)
#define IMU_CHECK_INTERVAL_MS 500UL
#define IMU_CONFIRM_COUNT 3
#define GRAV_MAJOR_TH 0.55f
#define GRAV_CROSS_TH 0.35f
uint8_t curRotation = 0;

// ---------- 全局状态 ----------

Config cfg;
Preferences prefs;
liangzi::LiangziUi ui;
liangzi::BalanceHistory balanceHistory;

bool timeSynced = false;           // 时间是否已可用（SNTP 或 HTTP Date 兜底）
bool ntpExactSynced = false;       // 是否已完成精确 SNTP 同步
bool wifiWasConnected = false;     // 上一帧 Wi-Fi 连接态（检测上升沿）
bool wifiSleepDisabled = false;    // 是否处于首同步低延迟模式
uint8_t ntpRetryCount = 0;         // 本轮未同步重试计数（用于轮转 3 台 NTP 服务器）
unsigned long wifiConnectedAt = 0; // 最近一次进入 WL_CONNECTED 的时间
volatile bool ntpCallbackFired = false;  // lwIP SNTP 回调触发标记（跨线程安全）
static char ntpPrimaryBuf[64] = DEFAULT_NTP;
bool dirty = true;
uint32_t dataRevision = 1;

// 余额状态（由 Core 0 balanceTask 查询，Core 1 唯一消费）
double balance = 0.0;
double grantedBalance = 0.0;
double toppedUpBalance = 0.0;
bool hasSplit = false;
std::string currency = "CNY";
bool balanceAvailable = true;
bool balanceValid = false;
bool balanceExpired = false;
int lastHttpStatus = 0;
double lastDelta = 0.0;
bool hasDelta = false;
unsigned long lastBalanceFetch = 0;
bool balanceForce = true;

// Core 0 异步请求队列与竞态代际号
struct BalanceJob {
  char apiKey[192];
  uint32_t generation;
};
struct BalanceResponse {
  liangzi::BalanceParseResult parsed;
  uint32_t generation;
};
QueueHandle_t balanceReqQ = nullptr;
QueueHandle_t balanceRespQ = nullptr;
bool balanceBusy = false;
bool balanceDiscard = false;
uint32_t balanceGen = 1;
#define BS_READ_SILENCE_MS 350UL
#define BS_READ_TOTAL_MS 6000UL

unsigned long lastWifiAttempt = 0;
unsigned long lastNtpAttempt = 0;
unsigned long lastStateSend = 0;
unsigned long lastDisplayTick = 0;
unsigned long lastPowerPoll = 0;

int batteryPct = -1;
bool chargingState = false;
int wifiRssi = -127;

// 长按 KEY2（700ms）蓄力旋屏
unsigned long btnBDownAt = 0;
bool btnBHoldFired = false;
float holdBProgress = 0.0f;

// ---------- 提示音状态（ADR-0004）----------
int8_t prevPeak = -1;       // -1=未知 1=峰 2=谷
double lastBalance = -1.0;  // 上次成功查询对比基准
bool mslAlertActive = false;

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
  liangzi::BeijingDateTime dt;
  liangzi::epochToBeijingDateTime(static_cast<int64_t>(now), dt);

  JsonDocument doc;
  doc["type"] = "state";
  doc["ts"] = now;
  char tbuf[32];
  snprintf(tbuf, sizeof(tbuf), "%04d-%02d-%02d %02d:%02d:%02d",
           dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
  doc["time"] = tbuf;
  if (timeSynced) {
    doc["phase"] = inPeakWindow(cfg, dt.year, dt.month, dt.day, dt.wday, dt.minuteOfDay)
                       ? "peak"
                       : "offpeak";
    doc["weekend"] = isWeekend(dt.wday);
    doc["holiday"] = isChinaPublicHoliday(dt.year, dt.month, dt.day);
  } else {
    doc["phase"] = "unknown";
  }
  doc["balance"] = balanceValid ? balance : -1;
  doc["balance_valid"] = balanceValid;
  doc["balance_expired"] = balanceExpired;
  if (balanceValid) {
    doc["granted_balance"] = hasSplit ? grantedBalance : -1;
    doc["topped_up_balance"] = hasSplit ? toppedUpBalance : -1;
    doc["currency"] = currency.c_str();
    doc["balance_available"] = balanceAvailable;
  }
  doc["wifi"] = WiFi.status() == WL_CONNECTED;
  doc["ssid"] = cfg.ssid;
  doc["ip"] = WiFi.localIP().toString();
  doc["battery"] = M5.Power.getBatteryLevel();
  doc["fw"] = FW_VERSION;
  doc["alert_enabled"] = cfg.alertEnabled;
  doc["rotation"] = curRotation;
  doc["auto_rotate"] = cfg.autoRotate;
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

void applyRotation(uint8_t rot);

static bool eqIgnoreCase(const char *a, const char *b) {
  while (*a && *b) {
    if (std::tolower(static_cast<unsigned char>(*a)) !=
        std::tolower(static_cast<unsigned char>(*b))) {
      return false;
    }
    ++a;
    ++b;
  }
  return *a == *b;
}

static void onNtpTimeSync(struct timeval * /*tv*/) {
  ntpCallbackFired = true;
}

static void setWifiLowLatency(bool enable) {
  if (wifiSleepDisabled == enable) return;
  WiFi.setSleep(!enable);
  wifiSleepDisabled = enable;
}

static void startNtpSync(uint32_t nowMs, uint8_t rotateIdx) {
  const char *primary = (cfg.ntp.length() > 0) ? cfg.ntp.c_str() : DEFAULT_NTP;
  std::snprintf(ntpPrimaryBuf, sizeof(ntpPrimaryBuf), "%s", primary);

  static const char *const kFallbackNtp[] = {
      "ntp.aliyun.com", "ntp.tencent.com", "cn.pool.ntp.org"};
  const char *pool[3] = {ntpPrimaryBuf, nullptr, nullptr};
  uint8_t count = 1;
  for (const char *cand : kFallbackNtp) {
    if (count >= 3) break;
    bool dup = false;
    for (uint8_t i = 0; i < count; ++i) {
      if (eqIgnoreCase(pool[i], cand)) {
        dup = true;
        break;
      }
    }
    if (!dup) pool[count++] = cand;
  }

  const uint8_t shift = rotateIdx % count;
  configTime(8 * 3600, 0,
             pool[shift], pool[(shift + 1u) % count], pool[(shift + 2u) % count]);
  lastNtpAttempt = nowMs;
}

void applyConfig() {
  WiFi.disconnect();
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
  if (cfg.hasWifi()) {
    WiFi.mode(WIFI_STA);
    setWifiLowLatency(true);
    WiFi.begin(cfg.ssid.c_str(), cfg.password.length() ? cfg.password.c_str() : NULL);
  } else {
    WiFi.mode(WIFI_OFF);
    wifiSleepDisabled = false;
  }
  lastWifiAttempt = millis();
  lastNtpAttempt = 0;
  wifiWasConnected = false;
  ntpExactSynced = false;
  ntpRetryCount = 0;
  ntpCallbackFired = false;

  // 使旧配置期间的在途 Core 0 余额查询失效并立即重查
  ++balanceGen;
  if (balanceBusy) balanceDiscard = true;
  balanceForce = true;
  balanceValid = false;
  balanceExpired = false;
  hasSplit = false;
  hasDelta = false;
  lastHttpStatus = 0;
  lastBalance = -1.0;
  balanceHistory.clear();
  ++dataRevision;
  dirty = true;

  if (!cfg.alertEnabled && mslAlertActive) {
    mslAlertActive = false;
    stopAlert();
  }
  applyRotation((uint8_t)cfg.screenRotation);
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
  cfg.alertEnabled = doc["alert_enabled"] | false;
  cfg.autoRotate = doc["auto_rotate"] | false;
  int rot = doc["screen_rotation"] | -1;
  if (rot >= 0 && rot <= 3) cfg.screenRotation = (uint8_t)rot;

  JsonArray ranges = doc["peak_ranges"];
  if (ranges.isNull()) {
    cfg.peakCount = 0;
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

// ---------- Core 0 后台余额查询 ----------

void balanceTask(void *) {
  BalanceJob *rawJob = nullptr;
  for (;;) {
    if (xQueueReceive(balanceReqQ, &rawJob, portMAX_DELAY) != pdTRUE) continue;
    std::unique_ptr<BalanceJob> job(rawJob);
    auto *resp = new (std::nothrow) BalanceResponse;
    if (!resp) abort();
    resp->generation = job->generation;

    WiFiClientSecure client;
    client.setCACert(ROOT_CA);
    client.setTimeout(6);

    if (client.connect(BALANCE_HOST, 443)) {
      client.print("GET " BALANCE_PATH " HTTP/1.1\r\n");
      client.print("Host: " BALANCE_HOST "\r\n");
      client.print("Authorization: Bearer ");
      client.print(job->apiKey);
      client.print("\r\nUser-Agent: liangzi-meter/" FW_VERSION "\r\nConnection: close\r\n\r\n");

      std::string raw;
      raw.reserve(1024);
      const uint32_t readStart = millis();
      uint32_t lastData = readStart;
      for (;;) {
        while (client.available() && raw.size() < 4096) {
          const int c = client.read();
          if (c < 0) break;
          raw.push_back(static_cast<char>(c));
          lastData = millis();
        }
        const uint32_t now = millis();
        if (!client.connected() && !client.available()) break;
        if (now - lastData > BS_READ_SILENCE_MS) break;
        if (now - readStart > BS_READ_TOTAL_MS) break;
        if (raw.size() >= 4096) break;
        vTaskDelay(pdMS_TO_TICKS(2));
      }
      client.stop();
      liangzi::parseDeepSeekBalanceHttp(raw.data(), raw.size(), resp->parsed);
    } else {
      client.stop();
    }

    if (xQueueSend(balanceRespQ, &resp, portMAX_DELAY) != pdTRUE) abort();
  }
}

// ---------- 网络与状态调度 ----------

void manageWiFi() {
  if (!cfg.hasWifi()) return;
  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiAttempt > 30000UL) {
    lastWifiAttempt = millis();
    WiFi.begin(cfg.ssid.c_str(), cfg.password.length() ? cfg.password.c_str() : NULL);
  }
}

void manageNtp(uint32_t now) {
  if (ntpCallbackFired) {
    ntpCallbackFired = false;
    ntpExactSynced = true;
    timeSynced = true;
    ntpRetryCount = 0;
    lastNtpAttempt = now;
    setWifiLowLatency(false);
    dirty = true;
  }

  const bool wifiNow = (WiFi.status() == WL_CONNECTED);
  if (!wifiNow) {
    wifiWasConnected = false;
    return;
  }

  const bool justConnected = !wifiWasConnected;
  if (justConnected) {
    wifiWasConnected = true;
    wifiConnectedAt = now;
    dirty = true;
    if (!ntpExactSynced) {
      setWifiLowLatency(true);
      ntpRetryCount = 1;
      startNtpSync(now, 0);
      return;
    }
  }

  if (wifiSleepDisabled && (ntpExactSynced || (now - wifiConnectedAt >= NTP_SLEEP_BOOST_MS))) {
    setWifiLowLatency(false);
  }

  if (ntpExactSynced) {
    if (now - lastNtpAttempt >= NTP_RESYNC_MS) {
      startNtpSync(now, 0);
    }
    return;
  }

  unsigned long retryInterval = NTP_SLOW_RETRY_MS;
  if (ntpRetryCount < 3) {
    retryInterval = NTP_FAST_RETRY_MS;
  } else if (timeSynced) {
    retryInterval = NTP_UPGRADE_RETRY_MS;
  }
  if (now - lastNtpAttempt >= retryInterval) {
    const uint8_t slot = ntpRetryCount % 3u;
    if (ntpRetryCount < 255) ++ntpRetryCount;
    startNtpSync(now, slot);
  }
}

bool inPeakWatch() {
  if (!cfg.alertEnabled) return false;
  time_t now = time(nullptr);
  if (now <= 1600000000L) return false;
  liangzi::BeijingDateTime dt;
  liangzi::epochToBeijingDateTime(static_cast<int64_t>(now), dt);
  return inPeakWindow(cfg, dt.year, dt.month, dt.day, dt.wday, dt.minuteOfDay);
}

void acceptBalanceResponse(uint32_t nowMs) {
  BalanceResponse *rawResp = nullptr;
  if (xQueueReceive(balanceRespQ, &rawResp, 0) != pdTRUE) return;
  std::unique_ptr<BalanceResponse> resp(rawResp);
  balanceBusy = false;

  if (resp->parsed.hasServerDate && !timeSynced && !ntpExactSynced) {
    struct timeval tv{};
    tv.tv_sec = static_cast<time_t>(resp->parsed.serverEpochSec);
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    timeSynced = true;
    dirty = true;
  }

  if (balanceDiscard || resp->generation != balanceGen) {
    balanceDiscard = false;
    balanceForce = true;
    dirty = true;
    return;
  }

  lastBalanceFetch = nowMs;
  balanceForce = false;

  if (resp->parsed.ok) {
    balance = resp->parsed.totalBalance;
    grantedBalance = resp->parsed.grantedBalance;
    toppedUpBalance = resp->parsed.toppedUpBalance;
    hasSplit = resp->parsed.hasSplit;
    currency = resp->parsed.currency.empty() ? "CNY" : resp->parsed.currency;
    balanceAvailable = resp->parsed.isAvailable;
    lastHttpStatus = resp->parsed.httpStatus;
    balanceValid = true;
    balanceExpired = false;

    const double prev = lastBalance;
    lastBalance = balance;
    const bool peakNow = inPeakWatch();
    const bool decreased = (prev >= 0.0) && (balance < prev - liangzi::kBalanceDecreaseEps);
    if (prev >= 0.0) {
      lastDelta = balance - prev;
      hasDelta = true;
    }

    balanceHistory.push(liangzi::BalanceSample{
        balance, grantedBalance, toppedUpBalance, nowMs, peakNow, decreased});

    const auto act = liangzi::evaluateRwrBalanceUpdate(peakNow, mslAlertActive, prev, balance);
    if (act == liangzi::RwrAction::kStartMissile) {
      mslAlertActive = true;
      playAlert(AL_MSL);
    } else if (act == liangzi::RwrAction::kStopMissile) {
      mslAlertActive = false;
      stopAlert();
    }
  } else {
    if (resp->parsed.httpStatus > 0) lastHttpStatus = resp->parsed.httpStatus;
    balanceExpired = balanceValid;
  }

  ++dataRevision;
  dirty = true;
}

void scheduleBalanceRequest(uint32_t nowMs) {
  if (balanceBusy) return;
  if (cfg.apiKey.length() == 0 || WiFi.status() != WL_CONNECTED) return;

  const unsigned long interval = inPeakWatch() ? PEAK_QUERY_INTERVAL_MS : BALANCE_INTERVAL_MS;
  const bool due = balanceForce || (lastBalanceFetch == 0) || (nowMs - lastBalanceFetch >= interval);
  if (!due) return;

  auto *job = new (std::nothrow) BalanceJob{};
  if (!job) return;
  std::snprintf(job->apiKey, sizeof(job->apiKey), "%s", cfg.apiKey.c_str());
  job->generation = balanceGen;
  if (xQueueSend(balanceReqQ, &job, 0) == pdTRUE) {
    balanceBusy = true;
    balanceForce = false;
    dirty = true;
  } else {
    delete job;
  }
}

// ---------- 显示与重力旋屏（ADR-0005）----------

void applyRotation(uint8_t rot) {
  if (rot > 3) rot = 0;
  if (rot == curRotation) return;
  curRotation = rot;
  M5.Display.setRotation(rot);
  ui.onResize(M5.Display.width(), M5.Display.height());
  dirty = true;
}

static int8_t rotationFromGravity(float ax, float ay) {
  float axa = fabsf(ax), aya = fabsf(ay);
  if (axa < GRAV_MAJOR_TH && aya < GRAV_MAJOR_TH) return -1;
  if (axa > aya) {
    if (aya > GRAV_CROSS_TH) return -1;
    return ax < 0 ? 0 : 2;
  } else {
    if (axa > GRAV_CROSS_TH) return -1;
    return ay < 0 ? 3 : 1;
  }
}

static int8_t pendingRotation = -1;
static uint8_t pendingCount = 0;
static unsigned long lastImuCheck = 0;

void manageRotation() {
  if (!cfg.autoRotate || !M5.Imu.isEnabled()) return;
  if (millis() - lastImuCheck < IMU_CHECK_INTERVAL_MS) return;
  lastImuCheck = millis();

  M5.Imu.update();
  float ax, ay, az;
  M5.Imu.getAccel(&ax, &ay, &az);

  int8_t target = rotationFromGravity(ax, ay);
  if (target < 0) {
    pendingRotation = -1;
    pendingCount = 0;
    return;
  }
  if (target == pendingRotation) {
    if (++pendingCount >= IMU_CONFIRM_COUNT) {
      pendingCount = 0;
      pendingRotation = -1;
      applyRotation((uint8_t)target);
    }
  } else {
    pendingRotation = target;
    pendingCount = 1;
  }
}

liangzi::UiModel buildUiModel(uint32_t nowMs) {
  liangzi::UiModel m;
  m.configured = cfg.hasWifi();
  m.wifiConnected = cfg.hasWifi() && (WiFi.status() == WL_CONNECTED);
  m.wifiRssi = m.wifiConnected ? wifiRssi : -127;
  m.ssid = cfg.ssid.c_str();
  m.ip = m.wifiConnected ? WiFi.localIP().toString().c_str() : "-";
  m.ntp = cfg.ntp.c_str();
  m.fwVersion = FW_VERSION;

  time_t nowSec = time(nullptr);
  const bool synced = nowSec > 1600000000L;
  if (synced != timeSynced) {
    timeSynced = synced;
    dirty = true;
  }
  m.timeSynced = synced;
  m.schedule = toPeakSchedule(cfg);
  if (synced) {
    liangzi::BeijingDateTime dt;
    liangzi::epochToBeijingDateTime(static_cast<int64_t>(nowSec), dt);
    m.year = dt.year;
    m.month = dt.month;
    m.day = dt.day;
    m.wday = dt.wday;
    m.hour = dt.hour;
    m.minute = dt.minute;
    m.second = dt.second;
    m.phaseKind = liangzi::classifyPhase(m.schedule, true, dt);
    m.countdownSec = liangzi::secondsToNextSwitch(m.schedule, static_cast<int64_t>(nowSec));
  } else {
    m.phaseKind = liangzi::PhaseKind::kUnsynced;
    m.countdownSec = -1;
  }

  m.balanceValid = balanceValid;
  m.balanceExpired = balanceExpired;
  m.balanceAvailable = balanceAvailable;
  m.queryBusy = balanceBusy;
  m.httpStatus = lastHttpStatus;
  m.currency = currency;
  m.totalBalance = balance;
  m.grantedBalance = grantedBalance;
  m.toppedUpBalance = toppedUpBalance;
  m.hasSplit = hasSplit;
  m.balanceWarn = cfg.balanceWarn;
  m.lastDelta = lastDelta;
  m.hasDelta = hasDelta;
  m.lastFetchMs = static_cast<uint32_t>(lastBalanceFetch);
  m.refreshIntervalMs = static_cast<uint32_t>(
      inPeakWatch() ? PEAK_QUERY_INTERVAL_MS : BALANCE_INTERVAL_MS);

  m.alertEnabled = cfg.alertEnabled;
  m.mslAlertActive = mslAlertActive;
  m.batteryPct = batteryPct;
  m.charging = chargingState;
  m.rotation = curRotation;
  m.autoRotate = cfg.autoRotate;
  m.holdBProgress = holdBProgress;
  m.history = &balanceHistory;
  m.dataRevision = dataRevision;
  (void)nowMs;
  return m;
}

// ---------- 按键 ----------

void handleKeys(uint32_t now) {
  if (M5.BtnA.wasClicked()) {
    // KEY1：保留当前余额不清空，触发顶栏青光扫屏与立即刷新
    ui.triggerPulse(now);
    if (cfg.apiKey.length()) balanceForce = true;
    dirty = true;
  }

  if (M5.BtnB.wasPressed()) {
    btnBDownAt = now;
    btnBHoldFired = false;
    holdBProgress = 0.0f;
  }
  if (M5.BtnB.wasHold()) {
    btnBHoldFired = true;
    holdBProgress = 0.0f;
    if (cfg.autoRotate) {
      ui.showToast("自动旋屏已开", now);
    } else {
      uint8_t nextRot = static_cast<uint8_t>((curRotation + 1u) & 3u);
      cfg.screenRotation = nextRot;
      applyRotation(nextRot);
      cfgSave(cfg, prefs);
      sendState();
    }
    dirty = true;
  } else if (M5.BtnB.isPressed() && !btnBHoldFired) {
    const unsigned long held = now - btnBDownAt;
    const float nextProg = (held >= 150UL)
                               ? std::min(1.0f, static_cast<float>(held - 150UL) / 550.0f)
                               : 0.0f;
    if (nextProg != holdBProgress) {
      holdBProgress = nextProg;
      dirty = true;
    }
  }
  if (M5.BtnB.wasReleased() && holdBProgress > 0.0f) {
    holdBProgress = 0.0f;
    dirty = true;
  }
  if (M5.BtnB.wasClicked()) {
    ui.nextPage(now);
    dirty = true;
  }
}

// ---------- 提示音事件（ADR-0004）----------

void manageAlert() {
  time_t now = time(nullptr);
  bool synced = now > 1600000000L;
  int img = 0;
  if (synced) {
    liangzi::BeijingDateTime dt;
    liangzi::epochToBeijingDateTime(static_cast<int64_t>(now), dt);
    img = inPeakWindow(cfg, dt.year, dt.month, dt.day, dt.wday, dt.minuteOfDay) ? 1 : 2;
  }

  if (cfg.alertEnabled) {
    if (img == 1 && prevPeak == 2) playAlert(AL_LOCK);
  }
  if (img != 0) prevPeak = img;

  if (mslAlertActive && (!cfg.alertEnabled || img != 1)) {
    mslAlertActive = false;
    stopAlert();
  }
}

// ---------- 主流程 ----------

void setup() {
  auto m5cfg = M5.config();
  M5.begin(m5cfg);  // 保留内置 IMU 与 Speaker（ADR-0004 / ADR-0005）
  M5.Display.setBrightness(160);
  M5.Speaker.setVolume(96);

  Serial.begin(115200);
  delay(300);

  cfgLoad(cfg, prefs);
  curRotation = (uint8_t)cfg.screenRotation;
  M5.Display.setRotation(curRotation);
  ui.begin(&M5.Display, M5.Display.width(), M5.Display.height(), true);
  M5.BtnB.setHoldThresh(700);

  balanceReqQ = xQueueCreate(1, sizeof(BalanceJob *));
  balanceRespQ = xQueueCreate(1, sizeof(BalanceResponse *));
  if (balanceReqQ && balanceRespQ) {
    xTaskCreatePinnedToCore(balanceTask, "lz-balance", 12288, nullptr, 1, nullptr, 0);
  }

  setenv("TZ", "CST-8", 1);
  tzset();
  esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
  esp_sntp_set_time_sync_notification_cb(onNtpTimeSync);

  if (cfg.hasWifi()) {
    WiFi.mode(WIFI_STA);
    setWifiLowLatency(true);
    WiFi.begin(cfg.ssid.c_str(), cfg.password.length() ? cfg.password.c_str() : NULL);
  } else {
    WiFi.mode(WIFI_OFF);
    wifiSleepDisabled = false;
  }
  lastWifiAttempt = millis();
  lastNtpAttempt = 0;

  sendHello();
}

void loop() {
  M5.update();
  const uint32_t now = millis();

  handleKeys(now);
  pumpSerial();
  manageWiFi();
  manageNtp(now);
  acceptBalanceResponse(now);
  scheduleBalanceRequest(now);
  manageRotation();
  manageAlert();
  alertTick();

  if (now - lastPowerPoll >= 5000UL || lastPowerPoll == 0) {
    batteryPct = M5.Power.getBatteryLevel();
    chargingState = M5.Power.isCharging();
    wifiRssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
    lastPowerPoll = now;
  }

  if (now - lastStateSend > STATE_INTERVAL_MS) {
    lastStateSend = now;
    sendState();
  }

  const liangzi::UiModel model = buildUiModel(now);
  const uint32_t interval = ui.recommendedFrameIntervalMs(model, now);
  if (dirty || (now - lastDisplayTick >= interval)) {
    ui.render(model, now, true);
    lastDisplayTick = now;
    dirty = false;
  }

  if (alertKind != AL_NONE || Serial.available()) {
    delay(2);
  } else {
    delay(interval <= 50u ? 2 : 10);
  }
}
