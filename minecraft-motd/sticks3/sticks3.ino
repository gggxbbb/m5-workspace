#include <M5Unified.h>
#include <WiFi.h>
#include <Preferences.h>
#include <MinecraftMotd.h>
#include <MotdSettings.h>
#include <MotdUi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <algorithm>
#include <deque>
#include <memory>
#include <new>

namespace {

constexpr uint32_t kBackground = 0x090D12;
constexpr uint32_t kRed = 0xF87171;

motd::MotdUi ui;
motd::TelemetryHistory history;
Preferences preferences;
QueueHandle_t requests;
QueueHandle_t responses;
std::unique_ptr<minecraft::Result> result;
minecraft::Settings settings;
struct ProbeJob { minecraft::Settings settings; uint32_t generation; };
struct ProbeResponse { minecraft::Result result; uint32_t generation; };
uint32_t generation = 0;
uint32_t resultRevision = 0;
int32_t prevOnline = -1;
int32_t playerDelta = 0;
uint32_t playerDeltaAt = 0;
std::string serialInput;
std::deque<std::string> serialOutput;
size_t serialOffset = 0;
uint32_t serialInputAt = 0;
uint32_t stateAt = 0;
bool inputOversize = false;
uint8_t rotation = 1;
bool busy = false;
bool discardResponse = false;
bool forceRefresh = true;
bool wifiWasConnected = false;
bool configured = false;
uint32_t finishedAt = 0;
uint32_t wifiAttemptAt = 0;
uint32_t lastPaintAt = 0;
uint32_t batteryAt = 0;
int battery = -1;
bool charging = false;
uint32_t btnBDownAt = 0;
bool btnBHoldFired = false;
float holdBProgress = 0.0f;
bool dirty = true;

class WifiTransport final : public minecraft::Transport {
 public:
  uint32_t nowMs() const override { return millis(); }
  bool resolve(const char* host, std::string& address) override {
    if (WiFi.hostByName(host, ip) != 1) return false;
    address = ip.toString().c_str();
    return true;
  }
  bool connect(uint16_t port, uint32_t timeoutMs) override {
    client.setConnectionTimeout(timeoutMs);
    if (!client.connect(ip, port, timeoutMs)) return false;
    client.setNoDelay(true);
    return true;
  }
  bool write(const uint8_t* data, size_t size, uint32_t timeoutMs) override {
    const uint32_t start = millis();
    client.setConnectionTimeout(timeoutMs);
    while (size) {
      if (millis() - start >= timeoutMs || !client.connected()) return false;
      const size_t sent = client.write(data, size);
      if (sent == 0) return false;
      data += sent;
      size -= sent;
    }
    return true;
  }
  bool read(uint8_t* data, size_t size, uint32_t timeoutMs) override {
    const uint32_t start = millis();
    while (size) {
      if (millis() - start >= timeoutMs) return false;
      const int available = client.available();
      if (available > 0) {
        const int count = client.read(data, std::min(size, size_t(available)));
        if (count <= 0) return false;
        data += count;
        size -= count;
      } else {
        if (!client.connected()) return false;
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }
    return true;
  }
  void close() override { client.stop(); }

 private:
  IPAddress ip;
  WiFiClient client;
};

void fatal(const char* message) {
  Serial.println(message);
  M5.Display.fillScreen(kBackground);
  M5.Display.setFont(&fonts::efontCN_14);
  M5.Display.setTextColor(kRed, kBackground);
  M5.Display.setTextWrap(true);
  M5.Display.setCursor(8, 12);
  M5.Display.print(message);
  while (true) { M5.update(); delay(10); }
}

void queryTask(void*) {
  ProbeJob* request;
  for (;;) {
    if (xQueueReceive(requests, &request, portMAX_DELAY) != pdTRUE) abort();
    std::unique_ptr<ProbeJob> job(request);
    auto* response = new (std::nothrow) ProbeResponse;
    if (!response) abort();
    WifiTransport transport;
    const auto& config = job->settings;
    response->generation = job->generation;
    response->result = minecraft::query(transport, config.host.c_str(),
                                       config.connectHost.empty() ? config.host.c_str() : config.connectHost.c_str(),
                                       config.port, config.protocol, config.timeoutMs);
    if (xQueueSend(responses, &response, portMAX_DELAY) != pdTRUE) abort();
  }
}

void sendJson(const JsonDocument& json) {
  std::string output;
  serializeJson(json, output);
  output += '\n';
  serialOutput.push_back(std::move(output));
}

void sendState(bool includeRaw) {
  JsonDocument json;
  json["type"] = "state";
  json["wifi"] = WiFi.status() == WL_CONNECTED;
  json["busy"] = busy;
  json["configured"] = configured;
  json["rotation"] = rotation * 90;
  if (result) {
    auto item = json["result"].to<JsonObject>();
    item["complete"] = result->complete;
    item["status_received"] = result->statusReceived;
    item["stage"] = minecraft::stageName(result->stage);
    item["error"] = result->error;
    item["dns_ms"] = result->dnsMs;
    item["tcp_ms"] = result->tcpMs;
    item["status_ms"] = result->statusMs;
    item["pong_ms"] = result->pongMs;
    item["online"] = result->online;
    item["max"] = result->maximum;
    item["version"] = result->version;
    item["age_seconds"] = (millis() - finishedAt) / 1000;
    if (includeRaw) item["raw_json"] = result->rawJson;
  }
  sendJson(json);
}

bool saveSettings(const minecraft::Settings& config) {
  JsonDocument json;
  minecraft::writeSettings(json.to<JsonObject>(), config, true);
  std::string storage;
  serializeJson(json, storage);
  return preferences.putString("settings", storage.c_str()) == storage.size();
}

void applySettings(minecraft::Settings config) {
  const bool networkChanged = config.ssid != settings.ssid || config.password != settings.password;
  const bool queryChanged = networkChanged || config.host != settings.host || config.connectHost != settings.connectHost ||
                            config.port != settings.port || config.protocol != settings.protocol || config.timeoutMs != settings.timeoutMs;
  settings = std::move(config);
  configured = settings.configured();
  rotation = settings.rotation;
  if (queryChanged) {
    ++generation;
    ++resultRevision;
    result.reset();
    history.clear();
    prevOnline = -1;
    playerDelta = 0;
    playerDeltaAt = 0;
    ui.resetMotdCycle(millis());
    if (busy) discardResponse = true;
    forceRefresh = true;
  }
  M5.Display.setRotation(rotation);
  M5.Display.setBrightness(settings.brightness);
  if (networkChanged) {
    wifiWasConnected = false;
    WiFi.disconnect();
    if (configured) WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
    wifiAttemptAt = millis();
  }
  dirty = true;
}

void rotateScreen() {
  settings.rotation = rotation == 1 ? 3 : 1;
  if (configured && !saveSettings(settings)) fatal("Failed to save screen rotation");
  rotation = settings.rotation;
  M5.Display.setRotation(rotation);
  if (M5.Display.width() != 240 || M5.Display.height() != 135) fatal("Invalid display dimensions");
  dirty = true;
}

void handleCommand(const std::string& input) {
  JsonDocument command;
  JsonDocument reply;
  reply["type"] = "reply";
  reply["ok"] = false;
  const auto error = deserializeJson(command, input, DeserializationOption::NestingLimit(8));
  if (error || !command["id"].is<uint32_t>() || !command["cmd"].is<const char*>()) {
    reply["error"] = "Invalid serial command format";
    sendJson(reply);
    return;
  }
  reply["id"] = command["id"];
  const std::string name = command["cmd"].as<const char*>();
  if (name == "get_config") {
    minecraft::writeSettings(reply["config"].to<JsonObject>(), settings);
  } else if (name == "configure") {
    minecraft::Settings next;
    std::string validation;
    if (!command["settings"].is<JsonObject>() ||
        !minecraft::readSettings(command["settings"].as<JsonObjectConst>(), settings, next, validation)) {
      reply["error"] = validation.empty() ? "Missing settings object" : validation;
      sendJson(reply);
      return;
    }
    if (!saveSettings(next)) {
      reply["error"] = "Failed to save settings";
      sendJson(reply);
      return;
    }
    applySettings(std::move(next));
    minecraft::writeSettings(reply["config"].to<JsonObject>(), settings);
  } else if (name == "refresh") {
    if (!configured || WiFi.status() != WL_CONNECTED || busy) {
      reply["error"] = "Check settings, Wi-Fi connection and query status";
      sendJson(reply);
      return;
    }
    forceRefresh = true;
  } else if (name == "get_state") {
    sendState(true);
  } else {
    reply["error"] = "Unknown command";
    sendJson(reply);
    return;
  }
  reply["ok"] = true;
  sendJson(reply);
}

void serialLoop(uint32_t now) {
  for (unsigned count = 0; count < 256 && Serial.available() && serialOutput.size() < 3; ++count) {
    const char character = Serial.read();
    serialInputAt = now;
    if (character == '\n') {
      if (inputOversize) {
        JsonDocument reply;
        reply["type"] = "reply";
        reply["ok"] = false;
        reply["error"] = "Serial command exceeds 4096 bytes";
        sendJson(reply);
      } else if (!serialInput.empty()) {
        handleCommand(serialInput);
      }
      serialInput.clear();
      inputOversize = false;
    } else if (character != '\r' && !inputOversize) {
      if (serialInput.size() >= 4096) { serialInput.clear(); inputOversize = true; }
      else serialInput += character;
    }
  }
  if ((!serialInput.empty() || inputOversize) && now - serialInputAt >= 3000 && serialOutput.size() < 3) {
    JsonDocument reply;
    reply["type"] = "reply";
    reply["ok"] = false;
    reply["error"] = "Serial command reception timed out";
    sendJson(reply);
    serialInput.clear();
    inputOversize = false;
  }
  if (!serialOutput.empty()) {
    const int available = Serial.availableForWrite();
    if (available > 0) {
      const auto& output = serialOutput.front();
      const size_t size = std::min({output.size() - serialOffset, size_t(available), size_t(256)});
      serialOffset += Serial.write(reinterpret_cast<const uint8_t*>(output.data() + serialOffset), size);
      if (serialOffset == output.size()) { serialOutput.pop_front(); serialOffset = 0; }
    }
  }
}

void acceptResponse(uint32_t now, bool connected) {
  ProbeResponse* response = nullptr;
  if (xQueueReceive(responses, &response, 0) != pdTRUE) return;
  busy = false;
  if (!response) fatal("Invalid query result");
  if (discardResponse || !connected || response->generation != generation) {
    delete response;
    discardResponse = false;
    forceRefresh = true;
    dirty = true;
    return;
  }
  result.reset(new (std::nothrow) minecraft::Result(std::move(response->result)));
  delete response;
  if (!result) fatal("Failed to allocate query result");

  history.push(motd::TelemetrySample{result->pongMs, result->online, result->complete, now});
  if (result->statusReceived && result->online >= 0) {
    if (prevOnline >= 0 && result->online != prevOnline) {
      playerDelta = result->online - prevOnline;
      playerDeltaAt = now;
    }
    prevOnline = result->online;
  }
  ++resultRevision;
  finishedAt = now;
  ui.resetMotdCycle(now);
  if (serialOutput.size() < 3) sendState(true);
  dirty = true;
}

motd::UiModel buildModel() {
  motd::UiModel m;
  m.configured = configured;
  m.wifiConnected = configured && WiFi.status() == WL_CONNECTED;
  m.busy = busy;
  m.charging = charging;
  m.batteryPct = battery;
  m.rotationDeg = static_cast<uint16_t>(rotation * 90);
  m.wifiAttemptAt = wifiAttemptAt;
  m.finishedAt = finishedAt;
  m.refreshMs = settings.refreshMs;
  m.holdBProgress = holdBProgress;
  m.host = settings.host;
  m.port = settings.port;
  m.protocol = settings.protocol;
  m.result = result.get();
  m.resultRevision = resultRevision;
  m.playerDelta = playerDelta;
  m.playerDeltaAt = playerDeltaAt;
  m.history = &history;
  return m;
}

}  // namespace

void setup() {
  auto config = M5.config();
  config.serial_baudrate = 115200;
  config.internal_mic = false;
  config.internal_spk = false;
  config.internal_imu = false;
  config.internal_rtc = false;
  M5.begin(config);
  if (!preferences.begin("minecraft-motd", false)) fatal("Failed to initialize settings storage");
  const String stored = preferences.getString("settings", "");
  if (!stored.isEmpty()) {
    JsonDocument json;
    std::string validation;
    if (deserializeJson(json, stored) ||
        !minecraft::readSettings(json.as<JsonObjectConst>(), settings, settings, validation)) fatal("Invalid saved settings");
  }
  rotation = settings.rotation;
  M5.Display.setRotation(rotation);
  if (M5.Display.width() != 240 || M5.Display.height() != 135) fatal("Invalid display dimensions");
  M5.Display.setBrightness(settings.brightness);
  if (!ui.begin(&M5.Display, true)) fatal("Failed to allocate display buffer");
  M5.BtnB.setHoldThresh(700);
  requests = xQueueCreate(1, sizeof(ProbeJob*));
  responses = xQueueCreate(1, sizeof(ProbeResponse*));
  if (!requests || !responses) fatal("Failed to create query queues");
  if (xTaskCreate(queryTask, "minecraft-query", 16384, nullptr, 1, nullptr) != pdPASS) fatal("Failed to create query task");
  configured = settings.configured();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (configured) {
    WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
    wifiAttemptAt = millis();
  }
}

void loop() {
  M5.update();
  const uint32_t now = millis();
  serialLoop(now);
  const bool connected = configured && WiFi.status() == WL_CONNECTED;

  if (M5.BtnB.wasPressed()) {
    btnBDownAt = now;
    btnBHoldFired = false;
    holdBProgress = 0.0f;
  }
  if (M5.BtnB.wasHold()) {
    btnBHoldFired = true;
    holdBProgress = 0.0f;
    rotateScreen();
  } else if (M5.BtnB.isPressed() && !btnBHoldFired) {
    const uint32_t held = now - btnBDownAt;
    const float nextProg = (held >= 150u) ? std::min(1.0f, static_cast<float>(held - 150u) / 550.0f) : 0.0f;
    if (nextProg != holdBProgress) {
      holdBProgress = nextProg;
      dirty = true;
    }
  }
  if (M5.BtnB.wasReleased()) {
    if (holdBProgress > 0.0f) {
      holdBProgress = 0.0f;
      dirty = true;
    }
  }
  if (M5.BtnB.wasClicked()) {
    ui.nextPage(now);
    dirty = true;
  }

  if (connected != wifiWasConnected) {
    if (!connected) {
      result.reset();
      ++resultRevision;
      if (busy) discardResponse = true;
    }
    wifiWasConnected = connected;
    forceRefresh = true;
    dirty = true;
  }
  if (M5.BtnA.wasClicked() && configured) {
    ui.triggerPulse(now);
    if (connected) { if (!busy) forceRefresh = true; }
    else { WiFi.disconnect(); WiFi.begin(settings.ssid.c_str(), settings.password.c_str()); wifiAttemptAt = now; }
    dirty = true;
  }
  acceptResponse(now, connected);
  if (connected && !busy && (forceRefresh || now - finishedAt >= settings.refreshMs)) {
    auto* request = new (std::nothrow) ProbeJob{settings, generation};
    if (!request) fatal("Failed to allocate query settings");
    if (xQueueSend(requests, &request, 0) != pdTRUE) fatal("Failed to submit query request");
    forceRefresh = false;
    busy = true;
    dirty = true;
  }
  if (now - batteryAt >= 5000 || batteryAt == 0) {
    battery = M5.Power.getBatteryLevel();
    charging = M5.Power.isCharging();
    batteryAt = now;
  }

  const motd::UiModel model = buildModel();
  const uint32_t frameInterval = ui.recommendedFrameIntervalMs(model, now);
  if (dirty || now - lastPaintAt >= frameInterval) {
    ui.render(model, now, true);
    dirty = false;
    lastPaintAt = now;
  }
  if (now - stateAt >= 2000 && serialOutput.empty() && Serial) {
    sendState(false);
    stateAt = now;
  }
  delay(frameInterval <= 50 ? 2 : 8);
}
