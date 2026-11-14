#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "net_types.h"

namespace xdnet {

std::string newRoomCode();
std::string normalizeRoomCode(const std::string&);
std::string formatRoomCode(const std::string& canonical);
extern const char* const kRoomAlphabet;

void roomInfohash(const std::string& canonicalCode, uint8_t out[20]);

struct SessionKeys {
  uint8_t sendKey[32];
  uint8_t recvKey[32];
  uint8_t transcript[32];
};

class Pake {
 public:
  Pake(const std::string& canonicalCode, uint16_t version);
  ~Pake();
  const uint8_t* publicValue() const { return y_; }
  bool finish(const uint8_t peerY[32], SessionKeys& out) const;

 private:
  uint8_t secret_[32];
  uint8_t y_[32];
  uint16_t version_;
};

void aeadSeal(const uint8_t key[32], uint64_t counter, const uint8_t* ad, size_t adSize, const uint8_t* plain,
              size_t size, uint8_t* out);
bool aeadOpen(const uint8_t key[32], uint64_t counter, const uint8_t* ad, size_t adSize, const uint8_t* cipher,
              size_t size, uint8_t* plainOut);

}
