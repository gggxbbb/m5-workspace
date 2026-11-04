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

struct Span {
  std::string text;
  uint32_t color = 0xF1D27E;
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
};

// connectHost 可指定实际连接目标；serverHost 保留握手中的服务器名称。
Result query(Transport& transport, const char* serverHost, const char* connectHost,
             uint16_t port, int32_t protocol = 47, uint32_t timeoutMs = 5000);
const char* stageName(Stage stage);

}
