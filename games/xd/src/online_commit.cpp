#include "online_commit.h"

#include <monocypher.h>

#include <cstdint>
#include <vector>

namespace xd::online {

std::string ToHex(const unsigned char* data, size_t size) {
  static const char* hex = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < size; i++) {
    out += hex[data[i] >> 4];
    out += hex[data[i] & 15];
  }
  return out;
}

bool FromHex(const std::string& hex, std::string& bytes) {
  if (hex.size() % 2 != 0) return false;
  auto val = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
  std::string out;
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int a = val(hex[i]), b = val(hex[i + 1]);
    if (a < 0 || b < 0) return false;
    out += static_cast<char>(a * 16 + b);
  }
  bytes = out;
  return true;
}

std::string Commit(const std::string& nonceHex, const std::string& record) {
  std::string nonce;
  if (!FromHex(nonceHex, nonce) || nonce.size() != kNonceBytes) return "";
  std::vector<uint8_t> msg(nonce.begin(), nonce.end());
  msg.insert(msg.end(), record.begin(), record.end());
  uint8_t hash[32];
  crypto_blake2b(hash, sizeof hash, msg.data(), msg.size());
  return ToHex(hash, sizeof hash);
}

bool Verify(const std::string& commitHex, const std::string& nonceHex, const std::string& record) {
  const std::string again = Commit(nonceHex, record);
  if (again.empty() || commitHex.size() != again.size()) return false;
  unsigned diff = 0;
  for (size_t i = 0; i < again.size(); i++) diff |= static_cast<unsigned>(again[i] ^ commitHex[i]);
  return diff == 0;
}

}
