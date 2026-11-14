#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

namespace xdnet {

constexpr uint16_t kProtocolVersion = 1;

struct Endpoint {
  uint8_t ip[16] = {};
  uint8_t family = 0;
  uint16_t port = 0;

  static Endpoint v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port);
  static bool parse(const std::string& text, Endpoint& out);
  bool valid() const { return family != 0 && port != 0; }
  std::string str() const;
  std::string maskedStr() const;
  bool operator==(const Endpoint& o) const {
    return family == o.family && port == o.port && std::memcmp(ip, o.ip, 16) == 0;
  }
  bool operator!=(const Endpoint& o) const { return !(*this == o); }
};

using SendFn = std::function<void(const Endpoint& to, const uint8_t* data, size_t size)>;

uint64_t nowMs();
void randomBytes(void* out, size_t size);

}
