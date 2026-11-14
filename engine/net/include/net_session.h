#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "net_crypto.h"
#include "net_types.h"

namespace xdnet {

constexpr size_t kMaxMessage = 65536;
constexpr size_t kMaxUnreliable = 1000;
constexpr size_t kMaxFingerprint = 400;
constexpr size_t kMaxDatagram = 1200;
constexpr size_t kHelloSize = 600;

enum class SessionState { Handshaking, Connected, Closed };
enum class CloseReason {
  None,
  Local,
  Remote,
  Timeout,
  HandshakeTimeout,
  ProtocolError,
};
enum class Issue { None, PeerVersionDiffers, WrongCodeOrTampered };

struct SessionConfig {
  uint16_t version = kProtocolVersion;
  std::vector<uint8_t> fingerprint;
  uint32_t handshakeTimeoutMs = 60000;
  uint32_t timeoutMs = 10000;
  uint32_t pingIntervalMs = 1000;
  uint32_t helloIntervalMs = 250;
  uint32_t maxPacketsPerSec = 4000;
  uint32_t maxAuthFailures = 12;
  uint32_t blockMs = 10000;
  size_t maxQueuedBytes = 1 << 20;
  size_t maxInboxMessages = 1024;
};

struct SessionStats {
  uint32_t rttMs = 0;
  uint32_t rttLastMs = 0;
  uint32_t jitterMs = 0;
  uint64_t packetsSent = 0, packetsReceived = 0;
  uint64_t packetsLost = 0;
  uint64_t retransmits = 0;
  uint64_t rejected = 0;
  uint64_t bytesSent = 0, bytesReceived = 0;
  uint64_t helloSent = 0, helloReceived = 0;
  uint32_t authFailures = 0;
  uint32_t blockedAddresses = 0;
};

class Session {
 public:
  Session(const std::string& canonicalRoomCode, const SessionConfig& cfg, SendFn send);
  ~Session();

  void addCandidate(const Endpoint& e);
  void onDatagram(const Endpoint& from, const uint8_t* data, size_t size, uint64_t nowMs);
  void update(uint64_t nowMs);

  bool sendReliable(const uint8_t* data, size_t size);
  bool sendUnreliable(const uint8_t* data, size_t size);
  bool receive(std::vector<uint8_t>& out, bool* reliable = nullptr);
  void close();

  SessionState state() const { return state_; }
  CloseReason closeReason() const { return closeReason_; }
  Issue issue() const { return issue_; }
  const SessionStats& stats() const { return stats_; }
  Endpoint peer() const { return peer_; }
  const std::vector<uint8_t>& peerFingerprint() const { return peerFingerprint_; }
  bool isLowSide() const { return weAreLow_; }

 private:
  struct Cand {
    Endpoint ep;
    uint64_t lastHello = 0;
    uint64_t lastReply = 0;
    uint32_t fails = 0;
    uint64_t blockedUntil = 0;
    bool listed = false;
    bool keyed = false;
    uint8_t theirY[32] = {};
    SessionKeys keys{};
  };
  struct OutFrame {
    uint32_t seq;
    bool more;
    std::vector<uint8_t> data;
    uint64_t sentAt = 0;
    uint32_t tx = 0;
    bool acked = false;
    bool fast = false;
  };
  struct InFrame {
    bool more;
    std::vector<uint8_t> data;
  };

  Cand* findCand(const Endpoint& e);
  void sendHello(Cand& c, uint64_t now);
  void sendConfirm(const Endpoint& to);
  void onHello(const Endpoint& from, const uint8_t* d, size_t n, uint64_t now);
  void onConfirm(const Endpoint& from, const uint8_t* d, size_t n, uint64_t now);
  void onData(const Endpoint& from, const uint8_t* d, size_t n, uint64_t now);
  void parseFrames(const uint8_t* p, size_t n, uint64_t now);
  void flush(uint64_t now, bool force);
  void sendPacket(const std::vector<uint8_t>& plain);
  void closeWith(CloseReason r);
  bool deliverReliable();

  SessionConfig cfg_;
  SendFn send_;
  std::string code_;
  Pake pake_;
  std::vector<Cand> cands_;
  SessionState state_ = SessionState::Handshaking;
  CloseReason closeReason_ = CloseReason::None;
  Issue issue_ = Issue::None;
  uint64_t startedAt_ = 0;
  bool started_ = false;
  uint64_t now_ = 0;

  int active_ = -1;
  bool weAreLow_ = false;
  Endpoint peer_;
  std::vector<uint8_t> peerFingerprint_;
  bool peerConfirmed_ = false;
  bool peerSeenData_ = false;
  uint64_t lastConfirmSent_ = 0;

  uint64_t sendCtr_ = 1;
  uint64_t recvHigh_ = 0;
  uint64_t recvMask_ = 0;
  uint64_t lastAuthRx_ = 0;
  uint64_t rateWindowStart_ = 0;
  uint32_t rateCount_ = 0;

  std::deque<OutFrame> out_;
  uint32_t nextSeq_ = 0;
  size_t queuedBytes_ = 0;
  uint32_t expectSeq_ = 0;
  std::map<uint32_t, InFrame> inWindow_;
  std::vector<uint8_t> partial_;
  struct Msg {
    std::vector<uint8_t> data;
    bool reliable;
  };
  std::deque<Msg> inbox_;
  std::deque<std::vector<uint8_t>> unreliableOut_;
  bool ackNeeded_ = false;
  uint32_t rto_ = 300;
  bool haveRtt_ = false;
  double srtt_ = 0, rttvar_ = 0;
  uint64_t lastSend_ = 0, lastPing_ = 0;
  std::vector<uint64_t> pongsToSend_;
  SessionStats stats_;
};

}
