#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace minecraft {

class Transport {
 public:
  virtual ~Transport() = default;
  virtual uint32_t nowMs() const = 0;
  virtual bool resolve(const char* host, std::string& address) = 0;
  virtual bool connect(uint16_t port, uint32_t timeoutMs) = 0;
  virtual bool write(const uint8_t* data, size_t size, uint32_t timeoutMs) = 0;
  virtual bool read(uint8_t* data, size_t size, uint32_t timeoutMs) = 0;
  virtual void close() = 0;
};

enum class Stage { Config, Dns, Tcp, Status, Pong, Done };

namespace Style {
constexpr uint8_t kNone = 0;
constexpr uint8_t kBold = 1 << 0;
constexpr uint8_t kItalic = 1 << 1;
constexpr uint8_t kUnderlined = 1 << 2;
constexpr uint8_t kStrikethrough = 1 << 3;
constexpr uint8_t kObfuscated = 1 << 4;
}

struct Span {
  std::string text;
  uint32_t color = 0xF1D27E;
  uint8_t style = Style::kNone;
};

using Line = std::vector<Span>;

struct Result {
  bool statusReceived = false;
  bool complete = false;
  Stage stage = Stage::Config;
  std::string error;
  std::string address;
  std::string version;
  std::string rawJson;
  int32_t online = -1;
  int32_t maximum = -1;
  int32_t protocol = -1;
  int32_t dnsMs = -1;
  int32_t tcpMs = -1;
  int32_t statusMs = -1;
  int32_t pongMs = -1;
  std::vector<Line> lines;
  std::vector<uint8_t> faviconPng;
  std::vector<std::string> playerSample;
};

// connectHost 可指定实际连接目标；serverHost 保留握手中的服务器名称。
Result query(Transport& transport, const char* serverHost, const char* connectHost,
             uint16_t port, int32_t protocol = 47, uint32_t timeoutMs = 5000);
bool parseStatusJson(const std::string& rawJson, Result& result);
const char* stageName(Stage stage);

}
