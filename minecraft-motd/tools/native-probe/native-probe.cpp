#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <MinecraftMotd.h>
#include <MotdSettings.h>
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace {

class SocketTransport final : public minecraft::Transport {
 public:
  ~SocketTransport() override { close(); }
  uint32_t nowMs() const override { return static_cast<uint32_t>(GetTickCount64()); }
  bool resolve(const char* host, std::string& address) override {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &addresses) != 0) return false;
    target = *reinterpret_cast<sockaddr_in*>(addresses->ai_addr);
    freeaddrinfo(addresses);
    char ip[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &target.sin_addr, ip, sizeof(ip))) return false;
    address = ip;
    return true;
  }
  bool connect(uint16_t port, uint32_t timeoutMs) override {
    socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) return false;
    u_long nonblocking = 1;
    if (ioctlsocket(socket, FIONBIO, &nonblocking)) return false;
    target.sin_port = htons(port);
    if (::connect(socket, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == 0) return true;
    if (WSAGetLastError() != WSAEWOULDBLOCK || !wait(true, timeoutMs)) return false;
    int error = 0;
    int size = sizeof(error);
    return getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) == 0 && error == 0;
  }
  bool write(const uint8_t* data, size_t size, uint32_t timeoutMs) override {
    uint32_t start = nowMs();
    while (size) {
      uint32_t elapsed = nowMs() - start;
      if (elapsed >= timeoutMs || !wait(true, timeoutMs - elapsed)) return false;
      int sent = send(socket, reinterpret_cast<const char*>(data), static_cast<int>(std::min(size, size_t(INT_MAX))), 0);
      if (sent <= 0) return false;
      data += sent;
      size -= sent;
    }
    return true;
  }
  bool read(uint8_t* data, size_t size, uint32_t timeoutMs) override {
    uint32_t start = nowMs();
    while (size) {
      uint32_t elapsed = nowMs() - start;
      if (elapsed >= timeoutMs || !wait(false, timeoutMs - elapsed)) return false;
      int count = recv(socket, reinterpret_cast<char*>(data), static_cast<int>(std::min(size, size_t(INT_MAX))), 0);
      if (count <= 0) return false;
      data += count;
      size -= count;
    }
    return true;
  }
  void close() override {
    if (socket != INVALID_SOCKET) { closesocket(socket); socket = INVALID_SOCKET; }
  }

 private:
  bool wait(bool writing, uint32_t timeoutMs) {
    fd_set descriptors;
    FD_ZERO(&descriptors);
    FD_SET(socket, &descriptors);
    fd_set errors = descriptors;
    timeval timeout{static_cast<long>(timeoutMs / 1000), static_cast<long>((timeoutMs % 1000) * 1000)};
    const int count = select(0, writing ? nullptr : &descriptors, writing ? &descriptors : nullptr, &errors, &timeout);
    return count > 0 && !FD_ISSET(socket, &errors) && FD_ISSET(socket, &descriptors);
  }
  SOCKET socket = INVALID_SOCKET;
  sockaddr_in target{};
};

bool number(const char* text, long minimum, long maximum, long& output) {
  char* end;
  output = std::strtol(text, &end, 10);
  return text != end && *end == '\0' && output >= minimum && output <= maximum;
}

}

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--validate-config") {
    std::string input;
    std::getline(std::cin, input);
    JsonDocument json;
    std::string error;
    minecraft::Settings settings;
    if (input.size() > 4096 || deserializeJson(json, input) ||
        !minecraft::readSettings(json.as<JsonObjectConst>(), settings, settings, error)) {
      std::fprintf(stderr, "invalid configuration: %s\n", error.c_str());
      return 1;
    }
    JsonDocument reply;
    minecraft::writeSettings(reply.to<JsonObject>(), settings);
    minecraft::Settings preserved;
    if (!minecraft::readSettings(reply.as<JsonObjectConst>(), settings, preserved, error) || preserved.password != settings.password) return 1;
    JsonDocument storage;
    minecraft::writeSettings(storage.to<JsonObject>(), settings, true);
    std::string stored;
    serializeJson(storage, stored);
    JsonDocument restored;
    minecraft::Settings loaded;
    if (deserializeJson(restored, stored) ||
        !minecraft::readSettings(restored.as<JsonObjectConst>(), {}, loaded, error) || loaded.password != settings.password) return 1;
    std::string output;
    serializeJson(reply, output);
    std::puts(output.c_str());
    return 0;
  }
  if (argc < 3 || argc > 5) {
    std::fprintf(stderr, "Usage: native-probe HOST PORT [CONNECT_HOST] [PROTOCOL]\n");
    return 2;
  }
  long port, protocol = 47;
  if (!number(argv[2], 1, 65535, port) || (argc == 5 && !number(argv[4], 0, INT_MAX, protocol))) return 2;
  SetConsoleOutputCP(CP_UTF8);
  WSADATA runtime;
  if (WSAStartup(MAKEWORD(2, 2), &runtime)) return 2;
  SocketTransport transport;
  const auto result = minecraft::query(transport, argv[1], argc >= 4 ? argv[3] : argv[1],
                                       static_cast<uint16_t>(port), static_cast<int32_t>(protocol));
  std::printf("complete=%d status=%d stage=%s address=%s\n", result.complete, result.statusReceived,
              minecraft::stageName(result.stage), result.address.c_str());
  std::printf("dns=%ld tcp=%ld status=%ld pong=%ld ms\n", static_cast<long>(result.dnsMs),
              static_cast<long>(result.tcpMs), static_cast<long>(result.statusMs), static_cast<long>(result.pongMs));
  std::printf("version=%s protocol=%ld players=%ld/%ld\n", result.version.c_str(), static_cast<long>(result.protocol),
              static_cast<long>(result.online), static_cast<long>(result.maximum));
  for (const auto& line : result.lines) {
    for (const auto& span : line) std::printf("[%06lx]%s", static_cast<unsigned long>(span.color), span.text.c_str());
    std::puts("");
  }
  if (!result.error.empty()) std::fprintf(stderr, "error=%s\n", result.error.c_str());
  WSACleanup();
  return result.complete ? 0 : 1;
}
