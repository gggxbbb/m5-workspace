#pragma once

#include <ArduinoJson.h>
#include <cstdint>
#include <string>

namespace minecraft {

struct Settings {
  std::string ssid;
  std::string password;
  std::string host;
  std::string connectHost;
  uint16_t port = 25565;
  int32_t protocol = 47;
  uint32_t refreshMs = 30000;
  uint32_t timeoutMs = 5000;
  uint8_t rotation = 1;
  uint8_t brightness = 150;
  bool configured() const { return !ssid.empty() && !host.empty(); }
};

bool readSettings(JsonObjectConst object, const Settings& previous, Settings& output, std::string& error);
void writeSettings(JsonObject object, const Settings& settings, bool includePassword = false);

}
