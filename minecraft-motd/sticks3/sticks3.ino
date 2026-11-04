#include <M5Unified.h>
#include <WiFi.h>
#include <Preferences.h>
#include <MinecraftMotd.h>
#include <MotdSettings.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <algorithm>
#include <memory>
#include <new>
#include <deque>

namespace {

constexpr uint32_t kBackground = 0x0D1720;
constexpr uint32_t kForeground = 0xE5EDF2;
constexpr uint32_t kMuted = 0xA8BAC8;
constexpr uint32_t kGreen = 0x76DFAA;
constexpr uint32_t kRed = 0xFF8080;
constexpr uint32_t kLine = 0x2B3B48;

M5Canvas canvas(&M5.Display);
Preferences preferences;
QueueHandle_t requests;
QueueHandle_t responses;
std::unique_ptr<minecraft::Result> result;
minecraft::Settings settings;
struct ProbeJob { minecraft::Settings settings; uint32_t generation; };
struct ProbeResponse { minecraft::Result result; uint32_t generation; };
uint32_t generation = 0;
std::string serialInput;
std::deque<std::string> serialOutput;
size_t serialOffset = 0;
uint32_t serialInputAt = 0;
uint32_t stateAt = 0;
bool inputOversize = false;
uint8_t rotation = 1;
bool details = false;
bool busy = false;
bool discardResponse = false;
bool forceRefresh = true;
bool wifiWasConnected = false;
bool configured = false;
uint32_t finishedAt = 0;
uint32_t wifiAttemptAt = 0;
uint32_t animationAt = 0;
uint32_t lastPaintAt = 0;
uint32_t batteryAt = 0;
int battery = -1;
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

void label(const char* text, int x, int y, uint32_t color = kForeground,
           const lgfx::IFont* font = &fonts::efontCN_12) {
  canvas.setFont(font);
  canvas.setTextColor(color, kBackground);
  canvas.drawString(text, x, y);
}

void rightLabel(const char* text, int right, int y, uint32_t color = kMuted,
                const lgfx::IFont* font = &fonts::efontCN_12) {
  canvas.setFont(font);
  label(text, right - canvas.textWidth(text), y, color, font);
}

void clippedLabel(const std::string& text, int x, int y, int width,
                  uint32_t color, const lgfx::IFont* font = &fonts::efontCN_12) {
  canvas.setClipRect(x, y, width, 18);
  label(text.c_str(), x, y, color, font);
  canvas.clearClipRect();
}

int motdWidth(const minecraft::Line& line) {
  canvas.setFont(&fonts::efontCN_14);
  int width = 0;
  for (const auto& span : line) width += canvas.textWidth(span.text.c_str());
  return width;
}

void drawMotdLine(const minecraft::Line& line, int y, uint32_t elapsed) {
  const int overflow = std::max(0, motdWidth(line) - 224);
  int offset = 0;
  if (overflow) {
    const uint32_t movement = uint32_t(overflow) * 45;
    const uint32_t cycle = elapsed % (movement * 2 + 3000);
    if (cycle > 1500 && cycle <= movement + 1500) offset = (cycle - 1500) / 45;
    else if (cycle > movement + 1500 && cycle <= movement + 3000) offset = overflow;
    else if (cycle > movement + 3000) offset = overflow - (cycle - movement - 3000) / 45;
  }
  int x = 8 - offset;
  canvas.setClipRect(8, y, 224, 18);
  for (const auto& span : line) {
    canvas.setTextColor(span.color, kBackground);
    canvas.drawString(span.text.c_str(), x, y);
    x += canvas.textWidth(span.text.c_str());
  }
  canvas.clearClipRect();
}

void drawHeader(bool connected) {
  const char* state = "Waiting";
  uint32_t color = kMuted;
  if (!configured) { state = "Setup"; color = kRed; }
  else if (!connected) { state = "Wi-Fi"; color = kRed; }
  else if (busy) { state = "Querying"; }
  else if (result && result->complete) { state = "Online"; color = kGreen; }
  else if (result && result->statusReceived) { state = "Pong failed"; color = kRed; }
  else if (result) { state = "Query failed"; color = kRed; }
  canvas.fillCircle(10, 9, 2, color);
  label(state, 17, 2, color, &fonts::efontCN_10);
  char text[40];
  if (battery >= 0) snprintf(text, sizeof(text), "%s  %d%%", connected ? "Wi-Fi" : "--", battery);
  else snprintf(text, sizeof(text), "%s  --", connected ? "Wi-Fi" : "--");
  rightLabel(text, 232, 2, kMuted, &fonts::efontCN_10);
}

void timing(const char* name, int32_t value, int x, int y) {
  label(name, x, y, kMuted);
  char text[32];
  if (value < 0) snprintf(text, sizeof(text), "--");
  else snprintf(text, sizeof(text), "%ld ms", static_cast<long>(value));
  label(text, x, y + 15, kForeground, &fonts::Font0);
}

void drawScreen(uint32_t now, bool connected) {
  canvas.fillSprite(kBackground);
  drawHeader(connected);
  if (!configured) {
    label("Configure with desktop app", 8, 38, kForeground, &fonts::efontCN_14);
    label("Connect USB to your PC", 8, 63, kMuted);
  } else if (!connected) {
    label(now - wifiAttemptAt < 15000 ? "Connecting to Wi-Fi" : "Wi-Fi connection failed", 8, 38,
          now - wifiAttemptAt < 15000 ? kForeground : kRed, &fonts::efontCN_14);
    label("A Reconnect", 8, 63, kMuted);
  } else if (details) {
    label("Query details", 8, 21, kForeground, &fonts::efontCN_14);
    if (result) {
      clippedLabel(result->address + ":" + std::to_string(settings.port), 8, 41, 224, kMuted);
      timing("DNS", result->dnsMs, 8, 60);
      timing("TCP", result->tcpMs, 120, 60);
      timing("Status", result->statusMs, 8, 88);
      timing("Ping/Pong", result->pongMs, 120, 88);
    } else {
      label(busy ? "Querying server" : "Waiting for query", 8, 56, kMuted);
    }
  } else {
    clippedLabel(settings.host, 8, 23, 164, kForeground, &fonts::efontCN_14);
    char latency[24];
    if (result && result->pongMs >= 0) snprintf(latency, sizeof(latency), "%ld ms", static_cast<long>(result->pongMs));
    else snprintf(latency, sizeof(latency), "-- ms");
    rightLabel(latency, 232, 24, kGreen);
    if (result && result->statusReceived) {
      const size_t pages = (result->lines.size() + 1) / 2;
      std::vector<uint32_t> durations;
      uint32_t totalDuration = 0;
      for (size_t page = 0; page < pages; ++page) {
        int overflow = 0;
        for (size_t i = page * 2; i < std::min(page * 2 + 2, result->lines.size()); ++i) {
          overflow = std::max(overflow, motdWidth(result->lines[i]) - 224);
        }
        const uint32_t duration = std::max(uint32_t(6000), uint32_t(overflow) * 90 + 3000);
        durations.push_back(duration);
        totalDuration += duration;
      }
      uint32_t elapsed = totalDuration ? (now - animationAt) % totalDuration : 0;
      size_t page = 0;
      while (page + 1 < pages && elapsed >= durations[page]) elapsed -= durations[page++];
      for (size_t i = 0; i < 2; ++i) {
        const size_t index = page * 2 + i;
        if (index < result->lines.size()) drawMotdLine(result->lines[index], 50 + i * 20, elapsed);
      }
      char players[40];
      if (result->online >= 0) snprintf(players, sizeof(players), "%ld / %ld", static_cast<long>(result->online), static_cast<long>(result->maximum));
      else snprintf(players, sizeof(players), "-- / --");
      canvas.setClipRect(8, 96, 120, 16);
      label(players, 8, 96);
      canvas.clearClipRect();
      canvas.setClipRect(132, 96, 100, 16);
      rightLabel(result->version.c_str(), 232, 96);
      canvas.clearClipRect();
    } else if (result) {
      const std::string error = std::string(minecraft::stageName(result->stage)) + ": " + result->error;
      canvas.setFont(&fonts::efontCN_14);
      canvas.setTextColor(kRed, kBackground);
      canvas.setTextWrap(true);
      canvas.setClipRect(8, 48, 224, 60);
      canvas.setCursor(8, 48);
      canvas.print(error.c_str());
      canvas.clearClipRect();
      canvas.setTextWrap(false);
    } else {
      label(busy ? "Querying server" : "Waiting for query", 8, 56, kMuted, &fonts::efontCN_14);
    }
  }
  canvas.drawFastHLine(8, 116, 224, kLine);
  label(details ? "A Refresh  B Back" : "A Refresh  B Details", 8, 120, kMuted, &fonts::efontCN_10);
  char age[24];
  if (busy) snprintf(age, sizeof(age), "Querying");
  else if (result) snprintf(age, sizeof(age), "%lus ago", static_cast<unsigned long>((now - finishedAt) / 1000));
  else snprintf(age, sizeof(age), "%d°", rotation * 90);
  rightLabel(age, 232, 120, kMuted, &fonts::efontCN_10);
  canvas.pushSprite(0, 0);
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
    result.reset();
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
  finishedAt = now;
  animationAt = now;
  if (serialOutput.size() < 3) sendState(true);
  dirty = true;
}

}

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
  canvas.setColorDepth(16);
  if (!canvas.createSprite(240, 135)) fatal("Failed to allocate display buffer");
  canvas.setTextWrap(false);
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
  if (M5.BtnB.wasHold()) rotateScreen();
  if (M5.BtnB.wasClicked()) { details = !details; dirty = true; }
  if (connected != wifiWasConnected) {
    if (!connected) {
      result.reset();
      if (busy) discardResponse = true;
    }
    wifiWasConnected = connected;
    forceRefresh = true;
    dirty = true;
  }
  if (M5.BtnA.wasClicked() && configured) {
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
    batteryAt = now;
  }
  if (dirty || now - lastPaintAt >= 100) {
    drawScreen(now, connected);
    dirty = false;
    lastPaintAt = now;
  }
  if (now - stateAt >= 2000 && serialOutput.empty() && Serial) {
    sendState(false);
    stateAt = now;
  }
  delay(5);
}
