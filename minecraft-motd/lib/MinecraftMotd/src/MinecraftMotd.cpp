#include <MinecraftMotd.h>
#include <ArduinoJson.h>
#include <array>
#include <cstring>
#include <cstdlib>

namespace minecraft {
namespace {

constexpr size_t kMaxPacket = 131072;
constexpr size_t kMaxSpans = 512;
constexpr size_t kMaxLines = 64;
constexpr uint32_t kColors[] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xFFAA00, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF};
constexpr const char* kColorNames[] = {
    "black", "dark_blue", "dark_green", "dark_aqua",
    "dark_red", "dark_purple", "gold", "gray",
    "dark_gray", "blue", "green", "aqua",
    "red", "light_purple", "yellow", "white"};

void varInt(std::vector<uint8_t>& bytes, uint32_t value) {
  do {
    uint8_t part = value & 0x7F;
    value >>= 7;
    bytes.push_back(part | (value ? 0x80 : 0));
  } while (value);
}

class Exchange {
 public:
  Exchange(Transport& transport, uint32_t timeout)
      : io(transport), start(io.nowMs()), timeout(timeout) {}

  bool read(uint8_t* bytes, size_t count) {
    const uint32_t elapsed = io.nowMs() - start;
    return elapsed < timeout && io.read(bytes, count, timeout - elapsed);
  }

  bool integer(uint32_t& value, size_t& count) {
    value = 0;
    count = 0;
    for (unsigned shift = 0; shift < 35; shift += 7) {
      uint8_t part;
      if (!read(&part, 1) || (shift == 28 && (part & 0xF0))) return false;
      ++count;
      value |= uint32_t(part & 0x7F) << shift;
      if (!(part & 0x80)) return true;
    }
    return false;
  }

  bool frame(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> bytes;
    varInt(bytes, static_cast<uint32_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    const uint32_t elapsed = io.nowMs() - start;
    return elapsed < timeout && io.write(bytes.data(), bytes.size(), timeout - elapsed);
  }

 private:
  Transport& io;
  uint32_t start;
  uint32_t timeout;
};

int hexDigit(char code) {
  if (code >= '0' && code <= '9') return code - '0';
  if (code >= 'a' && code <= 'f') return code - 'a' + 10;
  if (code >= 'A' && code <= 'F') return code - 'A' + 10;
  return -1;
}

bool colorValue(const char* name, uint32_t& color) {
  for (size_t i = 0; i < 16; ++i) {
    if (std::strcmp(name, kColorNames[i]) == 0) {
      color = kColors[i];
      return true;
    }
  }
  if (std::strlen(name) != 7 || name[0] != '#') return false;
  for (size_t i = 1; i < 7; ++i) if (hexDigit(name[i]) < 0) return false;
  color = std::strtoul(name + 1, nullptr, 16);
  return true;
}

class Motd {
 public:
  explicit Motd(Result& result) : result(result) { result.lines.emplace_back(); }

  bool component(JsonVariantConst value, uint32_t inherited = 0xF1D27E, unsigned depth = 0) {
    if (depth > 32) return false;
    if (value.is<const char*>()) return text(value.as<const char*>(), inherited);
    if (value.is<JsonArrayConst>()) {
      uint32_t parent = inherited;
      bool first = true;
      for (JsonVariantConst child : value.as<JsonArrayConst>()) {
        if (first && child.is<JsonObjectConst>() && child["color"].is<const char*>()) {
          if (!colorValue(child["color"].as<const char*>(), parent)) return false;
        }
        if (!component(child, parent, depth + 1)) return false;
        first = false;
      }
      return true;
    }
    if (!value.is<JsonObjectConst>()) return false;
    uint32_t color = inherited;
    if (value["color"].is<const char*>() && !colorValue(value["color"].as<const char*>(), color)) return false;
    if (!value["text"].isNull()) {
      if (!value["text"].is<const char*>() || !text(value["text"].as<const char*>(), color)) return false;
    } else if (!value["translate"].isNull()) {
      if (!value["translate"].is<const char*>() || !text(value["translate"].as<const char*>(), color)) return false;
    } else if (value["extra"].isNull()) {
      return false;
    }
    if (!value["extra"].isNull()) {
      if (!value["extra"].is<JsonArrayConst>()) return false;
      for (JsonVariantConst child : value["extra"].as<JsonArrayConst>()) {
        if (!component(child, color, depth + 1)) return false;
      }
    }
    return true;
  }

 private:
  bool append(const std::string& content, uint32_t color) {
    if (content.empty()) return true;
    if (++spans > kMaxSpans) return false;
    result.lines.back().push_back({content, color});
    return true;
  }

  bool text(const std::string& content, uint32_t color) {
    std::string chunk;
    for (size_t i = 0; i < content.size(); ++i) {
      if (content[i] == '\n') {
        if (!append(chunk, color) || result.lines.size() >= kMaxLines) return false;
        chunk.clear();
        result.lines.emplace_back();
      } else if (content[i] == '\r') {
        continue;
      } else if (content.compare(i, 2, "\xC2\xA7") == 0 && i + 2 < content.size()) {
        if (!append(chunk, color)) return false;
        chunk.clear();
        char code = content[i + 2];
        const int digit = hexDigit(code);
        if (digit >= 0) color = kColors[digit];
        else if (code == 'r' || code == 'R') color = 0xFFFFFF;
        // BungeeCord 使用连续六组颜色代码表达 RGB 颜色。
        else if ((code == 'x' || code == 'X') && i + 20 < content.size()) {
          uint32_t rgb = 0;
          for (size_t j = 0; j < 6; ++j) {
            size_t position = i + 3 + j * 3;
            if (content.compare(position, 2, "\xC2\xA7") != 0 || hexDigit(content[position + 2]) < 0) return false;
            rgb = (rgb << 4) | hexDigit(content[position + 2]);
          }
          color = rgb;
          i += 18;
        }
        i += 2;
      } else {
        chunk += content[i];
      }
    }
    return append(chunk, color);
  }

  Result& result;
  size_t spans = 0;
};

bool statusJson(Result& result) {
  JsonDocument json;
  const auto error = deserializeJson(json, result.rawJson, DeserializationOption::NestingLimit(40));
  if (error) { result.error = std::string("JSON: ") + error.c_str(); return false; }
  if (!json.is<JsonObject>() || !json["version"]["name"].is<const char*>() ||
      !json["version"]["protocol"].is<int32_t>() || json["description"].isNull()) {
    result.error = "Invalid server status fields";
    return false;
  }
  result.version = json["version"]["name"].as<const char*>();
  result.protocol = json["version"]["protocol"].as<int32_t>();
  if (!json["players"].isNull()) {
    if (!json["players"]["online"].is<int32_t>() || !json["players"]["max"].is<int32_t>() ||
        json["players"]["online"].as<int32_t>() < 0 || json["players"]["max"].as<int32_t>() < 0) {
      result.error = "Invalid player counts";
      return false;
    }
    result.online = json["players"]["online"].as<int32_t>();
    result.maximum = json["players"]["max"].as<int32_t>();
  }
  Motd motd(result);
  if (!motd.component(json["description"])) {
    result.error = "Unsupported MOTD type or size";
    return false;
  }
  return true;
}

}

const char* stageName(Stage stage) {
  switch (stage) {
    case Stage::Config: return "Settings";
    case Stage::Dns: return "DNS";
    case Stage::Tcp: return "TCP";
    case Stage::Status: return "Status";
    case Stage::Pong: return "Ping/Pong";
    case Stage::Done: return "Done";
  }
  return "Unknown";
}

Result query(Transport& io, const char* serverHost, const char* connectHost,
             uint16_t port, int32_t protocol, uint32_t timeoutMs) {
  Result result;
  struct Close { Transport& io; ~Close() { io.close(); } } close{io};
  if (!serverHost || !connectHost || !*serverHost || !*connectHost ||
      std::strlen(serverHost) > 255 || !port || protocol < 0 || timeoutMs == 0) {
    result.error = "Invalid server settings";
    return result;
  }
  result.stage = Stage::Dns;
  uint32_t start = io.nowMs();
  if (!io.resolve(connectHost, result.address)) { result.error = "DNS lookup failed"; return result; }
  result.dnsMs = io.nowMs() - start;
  result.stage = Stage::Tcp;
  start = io.nowMs();
  if (!io.connect(port, timeoutMs)) { result.error = "Connection failed or timed out"; return result; }
  result.tcpMs = io.nowMs() - start;
  result.stage = Stage::Status;
  start = io.nowMs();
  Exchange exchange(io, timeoutMs);
  std::vector<uint8_t> handshake{0};
  varInt(handshake, static_cast<uint32_t>(protocol));
  const size_t hostLength = std::strlen(serverHost);
  varInt(handshake, static_cast<uint32_t>(hostLength));
  handshake.insert(handshake.end(), serverHost, serverHost + hostLength);
  handshake.push_back(port >> 8);
  handshake.push_back(port & 0xFF);
  handshake.push_back(1);
  if (!exchange.frame(handshake) || !exchange.frame({0})) {
    result.error = "Failed to send status request"; return result;
  }
  uint32_t packetLength, packetId, stringLength;
  size_t prefixSize, idSize, stringSize;
  if (!exchange.integer(packetLength, prefixSize) || packetLength < 3 || packetLength > kMaxPacket ||
      !exchange.integer(packetId, idSize) || packetId != 0 ||
      !exchange.integer(stringLength, stringSize) || stringLength == 0 ||
      stringLength > kMaxPacket || idSize + stringSize + stringLength != packetLength) {
    result.error = "Invalid status packet or timeout"; return result;
  }
  result.rawJson.resize(stringLength);
  if (!exchange.read(reinterpret_cast<uint8_t*>(&result.rawJson[0]), stringLength)) {
    result.error = "Status response interrupted or timed out"; return result;
  }
  result.statusMs = io.nowMs() - start;
  if (!statusJson(result)) return result;
  result.statusReceived = true;

  result.stage = Stage::Pong;
  Exchange ping(io, timeoutMs);
  std::vector<uint8_t> payload{1};
  const uint64_t nonce = (uint64_t(io.nowMs()) << 32) | 0x4D354D43;
  for (int shift = 56; shift >= 0; shift -= 8) payload.push_back(static_cast<uint8_t>(nonce >> shift));
  start = io.nowMs();
  if (!ping.frame(payload)) { result.error = "Failed to send Ping"; return result; }
  std::array<uint8_t, 8> echo;
  if (!ping.integer(packetLength, prefixSize) || packetLength != 9 ||
      !ping.integer(packetId, idSize) || packetId != 1 || idSize != 1 ||
      !ping.read(echo.data(), echo.size()) || std::memcmp(echo.data(), payload.data() + 1, 8)) {
    result.error = "Invalid Pong, disconnect or timeout"; return result;
  }
  result.pongMs = io.nowMs() - start;
  result.stage = Stage::Done;
  result.complete = true;
  return result;
}

}
