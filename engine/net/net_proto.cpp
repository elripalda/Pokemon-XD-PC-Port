#include "net_proto.h"

#include <cstring>

namespace xdnet {

namespace {
void p16(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back((uint8_t)(x >> 8));
  v.push_back((uint8_t)x);
}
void p32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 3; i >= 0; i--) v.push_back((uint8_t)(x >> (8 * i)));
}
void p64(std::vector<uint8_t>& v, uint64_t x) {
  for (int i = 7; i >= 0; i--) v.push_back((uint8_t)(x >> (8 * i)));
}
uint32_t g16(const uint8_t* p) { return (uint32_t)p[0] << 8 | p[1]; }
uint32_t g32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint64_t g64(const uint8_t* p) { return (uint64_t)g32(p) << 32 | g32(p + 4); }
constexpr uint32_t kCookie = 0x2112A442;
}

std::vector<uint8_t> stunBuildRequest(const uint8_t tid[12]) {
  std::vector<uint8_t> v;
  p16(v, 0x0001);
  p16(v, 0);
  p32(v, kCookie);
  v.insert(v.end(), tid, tid + 12);
  return v;
}

bool looksLikeStun(const uint8_t* d, size_t n) { return n >= 20 && (d[0] & 0xC0) == 0 && g32(d + 4) == kCookie; }

bool stunParseResponse(const uint8_t* d, size_t n, const uint8_t tid[12], Endpoint& mapped) {
  if (!looksLikeStun(d, n) || g16(d) != 0x0101) return false;
  if (std::memcmp(d + 8, tid, 12) != 0) return false;
  size_t len = g16(d + 2);
  if (20 + len > n) return false;
  size_t i = 20;
  bool have = false;
  Endpoint plain;
  while (i + 4 <= 20 + len) {
    uint32_t t = g16(d + i), l = g16(d + i + 2);
    i += 4;
    if (i + l > 20 + len) return false;
    if ((t == 0x0020 || t == 0x0001) && l >= 8 && d[i + 1] == 0x01) {
      Endpoint e;
      e.family = 4;
      if (t == 0x0020) {
        e.port = (uint16_t)(g16(d + i + 2) ^ (kCookie >> 16));
        uint32_t a = g32(d + i + 4) ^ kCookie;
        for (int k = 0; k < 4; k++) e.ip[k] = (uint8_t)(a >> (24 - 8 * k));
        mapped = e;
        return e.valid();
      }
      e.port = (uint16_t)g16(d + i + 2);
      std::memcpy(e.ip, d + i + 4, 4);
      plain = e;
      have = true;
    }
    i += (l + 3) & ~3u;
  }
  if (have && plain.valid()) {
    mapped = plain;
    return true;
  }
  return false;
}

std::vector<uint8_t> trackerBuildConnect(uint32_t tx) {
  std::vector<uint8_t> v;
  p64(v, 0x41727101980ull);
  p32(v, 0);
  p32(v, tx);
  return v;
}

bool trackerParseConnect(const uint8_t* d, size_t n, uint32_t tx, uint64_t& id) {
  if (n < 16 || g32(d) != 0 || g32(d + 4) != tx) return false;
  id = g64(d + 8);
  return true;
}

std::vector<uint8_t> trackerBuildAnnounce(uint64_t cid, uint32_t tx, const uint8_t ih[20], const uint8_t pid[20],
                                          TrackerEvent ev, uint32_t key, uint16_t port) {
  std::vector<uint8_t> v;
  p64(v, cid);
  p32(v, 1);
  p32(v, tx);
  v.insert(v.end(), ih, ih + 20);
  v.insert(v.end(), pid, pid + 20);
  p64(v, 0);
  p64(v, 1);
  p64(v, 0);
  p32(v, ev);
  p32(v, 0);
  p32(v, key);
  p32(v, 20);
  p16(v, port);
  return v;
}

bool trackerParseAnnounce(const uint8_t* d, size_t n, uint32_t tx, uint32_t& interval, std::vector<Endpoint>& peers) {
  if (n < 20 || g32(d) != 1 || g32(d + 4) != tx) return false;
  interval = g32(d + 8);
  peers.clear();
  for (size_t i = 20; i + 6 <= n && peers.size() < 100; i += 6) {
    Endpoint e = Endpoint::v4(d[i], d[i + 1], d[i + 2], d[i + 3], (uint16_t)g16(d + i + 4));
    if (e.valid()) peers.push_back(e);
  }
  return true;
}

int trackerPeekAction(const uint8_t* d, size_t n, uint32_t& tx) {
  if (n < 8) return -1;
  uint32_t a = g32(d);
  tx = g32(d + 4);
  return (a == 0 || a == 1 || a == 3) ? (int)a : -1;
}

}
