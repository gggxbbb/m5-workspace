#include <MotdSettings.h>
#include <utility>

namespace minecraft {
namespace {

bool hostName(const std::string& value) {
  if (value.empty() || value.size() > 253) return false;
  for (unsigned char character : value) {
    if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
          (character >= '0' && character <= '9') || character == '-' || character == '.' || character == '_')) return false;
  }
  return true;
}

bool integer(JsonObjectConst object, const char* key, int32_t low, int32_t high) {
  return object[key].is<int32_t>() && object[key].as<int32_t>() >= low && object[key].as<int32_t>() <= high;
}

}

bool readSettings(JsonObjectConst object, const Settings& previous, Settings& output, std::string& error) {
  if (!object["ssid"].is<const char*>() || !object["host"].is<const char*>() ||
      !object["connect_host"].is<const char*>()) { error = "配置字段类型无效"; return false; }
  Settings next;
  next.ssid = object["ssid"].as<std::string>();
  next.host = object["host"].as<std::string>();
  next.connectHost = object["connect_host"].as<std::string>();
  if (next.ssid.empty() || next.ssid.size() > 32 || next.ssid.find('\0') != std::string::npos) {
    error = "Wi-Fi 名称必须为 1~32 字节且内容有效"; return false;
  }
  if (!hostName(next.host) || (!next.connectHost.empty() && !hostName(next.connectHost))) {
    error = "服务器地址应填写 IPv4 地址或域名"; return false;
  }
  if (object["password"].isNull()) {
    if (next.ssid != previous.ssid) { error = "更换 Wi-Fi 时需要填写密码或选择开放网络"; return false; }
    next.password = previous.password;
  } else {
    if (!object["password"].is<const char*>()) { error = "Wi-Fi 密码类型无效"; return false; }
    next.password = object["password"].as<std::string>();
  }
  if ((!next.password.empty() && (next.password.size() < 8 || next.password.size() > 63)) ||
      next.password.find('\0') != std::string::npos) {
    error = "Wi-Fi 密码必须为 8~63 字节"; return false;
  }
  if (!integer(object, "port", 1, 65535) || !integer(object, "protocol", 0, INT32_MAX) ||
      !integer(object, "refresh_seconds", 10, 3600) || !integer(object, "timeout_seconds", 1, 30) ||
      !integer(object, "brightness", 20, 255) || !integer(object, "rotation", 90, 270) ||
      (object["rotation"].as<int>() != 90 && object["rotation"].as<int>() != 270)) {
    error = "端口、协议、时间、亮度或方向超出范围"; return false;
  }
  next.port = object["port"].as<uint16_t>();
  next.protocol = object["protocol"].as<int32_t>();
  next.refreshMs = object["refresh_seconds"].as<uint32_t>() * 1000;
  next.timeoutMs = object["timeout_seconds"].as<uint32_t>() * 1000;
  next.rotation = static_cast<uint8_t>(object["rotation"].as<int>() / 90);
  next.brightness = object["brightness"].as<uint8_t>();
  output = std::move(next);
  return true;
}

void writeSettings(JsonObject object, const Settings& settings, bool includePassword) {
  object["ssid"] = settings.ssid;
  object["host"] = settings.host;
  object["connect_host"] = settings.connectHost;
  object["port"] = settings.port;
  object["protocol"] = settings.protocol;
  object["refresh_seconds"] = settings.refreshMs / 1000;
  object["timeout_seconds"] = settings.timeoutMs / 1000;
  object["rotation"] = settings.rotation * 90;
  object["brightness"] = settings.brightness;
  if (includePassword) object["password"] = settings.password;
  else object["password_set"] = !settings.password.empty();
}

}
