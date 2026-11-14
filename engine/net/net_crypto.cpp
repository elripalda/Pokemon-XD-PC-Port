#include "net_crypto.h"

#include <cstdlib>
#include <cstring>

extern "C" {
#include "monocypher.h"
}

namespace xdnet {

const char* const kRoomAlphabet = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
static constexpr int kAlphaN = 31;
static constexpr int kCodeLen = 6;

static int symbolValue(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  for (int i = 0; i < kAlphaN; i++)
    if (kRoomAlphabet[i] == c) return i;
  return -1;
}

std::string newRoomCode() {
  std::string s;
  while ((int)s.size() < kCodeLen) {
    uint8_t b;
    randomBytes(&b, 1);
    if (b >= 248) continue;
    s += kRoomAlphabet[b % kAlphaN];
  }
  return s;
}

std::string normalizeRoomCode(const std::string& in) {
  std::string s;
  for (char c : in) {
    if (c == '-' || c == ' ' || c == '_') continue;
    int x = symbolValue(c);
    if (x < 0 || (int)s.size() >= kCodeLen) return "";
    s += kRoomAlphabet[x];
  }
  return (int)s.size() == kCodeLen ? s : "";
}

std::string formatRoomCode(const std::string& c) {
  if (c.size() != kCodeLen) return c;
  return c.substr(0, 3) + "-" + c.substr(3, 3);
}

void roomInfohash(const std::string& code, uint8_t out[20]) {
  const uint32_t blocks = 32768;
  uint8_t* work = (uint8_t*)std::malloc((size_t)blocks * 1024);
  if (!work) std::abort();
  uint8_t hash[32];
  static const uint8_t salt[16] = {'X', 'D', 'N', 'E', 'T', '-', 'r', 'o', 'o', 'm', '-', 'v', '1', 0, 0, 0};
  crypto_argon2_config cfg = {CRYPTO_ARGON2_ID, blocks, 3, 1};
  crypto_argon2_inputs in = {(const uint8_t*)code.data(), salt, (uint32_t)code.size(), (uint32_t)sizeof salt};
  crypto_argon2(hash, 32, work, cfg, in, crypto_argon2_no_extras);
  std::free(work);
  std::memcpy(out, hash, 20);
}

static void pointFromCode(const std::string& code, uint16_t version, uint8_t g[32]) {
  crypto_blake2b_ctx c;
  crypto_blake2b_init(&c, 32);
  static const char dsi[] = "XDNET1 CPace25519 generator";
  crypto_blake2b_update(&c, (const uint8_t*)dsi, sizeof dsi);
  uint8_t vv[2] = {(uint8_t)(version >> 8), (uint8_t)version};
  crypto_blake2b_update(&c, vv, 2);
  crypto_blake2b_update(&c, (const uint8_t*)code.data(), code.size());
  uint8_t h[32];
  crypto_blake2b_final(&c, h);
  crypto_elligator_map(g, h);
}

static bool allZero(const uint8_t* p, size_t n) {
  uint8_t x = 0;
  for (size_t i = 0; i < n; i++) x |= p[i];
  return x == 0;
}

Pake::Pake(const std::string& code, uint16_t version) : version_(version) {
  randomBytes(secret_, 32);
  uint8_t g[32];
  pointFromCode(code, version, g);
  crypto_x25519(y_, secret_, g);
  crypto_wipe(g, 32);
}

Pake::~Pake() {
  crypto_wipe(secret_, sizeof secret_);
  crypto_wipe(y_, sizeof y_);
}

bool Pake::finish(const uint8_t peerY[32], SessionKeys& out) const {
  if (std::memcmp(peerY, y_, 32) == 0) return false;
  uint8_t k[32];
  crypto_x25519(k, secret_, peerY);
  if (allZero(k, 32)) return false;
  const uint8_t* lo = std::memcmp(y_, peerY, 32) < 0 ? y_ : peerY;
  const uint8_t* hi = lo == y_ ? peerY : y_;
  bool weAreLow = lo == y_;

  uint8_t vv[2] = {(uint8_t)(version_ >> 8), (uint8_t)version_};
  uint8_t isk[64];
  crypto_blake2b_ctx c;
  crypto_blake2b_init(&c, 64);
  static const char dsi[] = "XDNET1 session keys";
  crypto_blake2b_update(&c, (const uint8_t*)dsi, sizeof dsi);
  crypto_blake2b_update(&c, vv, 2);
  crypto_blake2b_update(&c, k, 32);
  crypto_blake2b_update(&c, lo, 32);
  crypto_blake2b_update(&c, hi, 32);
  crypto_blake2b_final(&c, isk);
  std::memcpy(out.sendKey, weAreLow ? isk : isk + 32, 32);
  std::memcpy(out.recvKey, weAreLow ? isk + 32 : isk, 32);

  crypto_blake2b_init(&c, 32);
  static const char dsi2[] = "XDNET1 transcript";
  crypto_blake2b_update(&c, (const uint8_t*)dsi2, sizeof dsi2);
  crypto_blake2b_update(&c, vv, 2);
  crypto_blake2b_update(&c, lo, 32);
  crypto_blake2b_update(&c, hi, 32);
  crypto_blake2b_final(&c, out.transcript);
  crypto_wipe(isk, sizeof isk);
  crypto_wipe(k, sizeof k);
  return true;
}

static void nonceOf(uint64_t counter, uint8_t n[24]) {
  std::memset(n, 0, 24);
  for (int i = 0; i < 8; i++) n[i] = (uint8_t)(counter >> (8 * i));
}

void aeadSeal(const uint8_t key[32], uint64_t counter, const uint8_t* ad, size_t adSize, const uint8_t* plain,
              size_t size, uint8_t* out) {
  uint8_t n[24];
  nonceOf(counter, n);
  crypto_aead_lock(out, out + size, key, n, ad, adSize, plain, size);
}

bool aeadOpen(const uint8_t key[32], uint64_t counter, const uint8_t* ad, size_t adSize, const uint8_t* cipher,
              size_t size, uint8_t* plainOut) {
  if (size < 16) return false;
  uint8_t n[24];
  nonceOf(counter, n);
  size_t m = size - 16;
  return crypto_aead_unlock(plainOut, cipher + m, key, n, ad, adSize, cipher, m) == 0;
}

}
