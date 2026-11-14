#include "net_types.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <stdlib.h>
#elif defined(__linux__)
#include <sys/random.h>
#endif
#endif

namespace xdnet {

Endpoint Endpoint::v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port) {
  Endpoint e;
  e.family = 4;
  e.ip[0] = a;
  e.ip[1] = b;
  e.ip[2] = c;
  e.ip[3] = d;
  e.port = port;
  return e;
}

bool Endpoint::parse(const std::string& text, Endpoint& out) {
  unsigned a, b, c, d, p;
  char tail = 0;
  if (std::sscanf(text.c_str(), "%u.%u.%u.%u:%u%c", &a, &b, &c, &d, &p, &tail) != 5) return false;
  if (a > 255 || b > 255 || c > 255 || d > 255 || p == 0 || p > 65535) return false;
  out = v4((uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d, (uint16_t)p);
  return true;
}

std::string Endpoint::str() const {
  char buf[64];
  if (family == 4)
    std::snprintf(buf, sizeof buf, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], port);
  else if (family == 6) {
    std::snprintf(buf, sizeof buf, "[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]:%u", ip[0], ip[1],
                  ip[2], ip[3], ip[4], ip[5], ip[6], ip[7], ip[8], ip[9], ip[10], ip[11], ip[12], ip[13], ip[14], ip[15], port);
  } else
    std::snprintf(buf, sizeof buf, "(none)");
  return buf;
}

std::string Endpoint::maskedStr() const {
  char buf[32];
  if (family == 4) {
    std::snprintf(buf, sizeof buf, "%u.%u.x.x", ip[0], ip[1]);
    return buf;
  }
  if (family == 6) {
    std::snprintf(buf, sizeof buf, "%02x%02x:x:x:x", ip[0], ip[1]);
    return buf;
  }
  return "(none)";
}

uint64_t nowMs() {
  using namespace std::chrono;
  return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void randomBytes(void* out, size_t size) {
  bool ok = false;
#if defined(_WIN32)
  ok = BCryptGenRandom(nullptr, (PUCHAR)out, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__APPLE__)
  arc4random_buf(out, size);
  ok = true;
#else
  size_t got = 0;
  while (got < size) {
#if defined(__linux__)
    ssize_t n = getrandom((uint8_t*)out + got, size - got, 0);
    if (n <= 0) break;
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) break;
    ssize_t n = read(fd, (uint8_t*)out + got, size - got);
    close(fd);
    if (n <= 0) break;
#endif
    got += (size_t)n;
  }
  ok = got == size;
#endif
  if (!ok) {
    std::fprintf(stderr, "[net] no operating-system random numbers: stopping\n");
    std::abort();
  }
}

}
