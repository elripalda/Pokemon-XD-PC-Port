#pragma once
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net_session.h"

namespace xdnet {

enum class Phase {
  Idle,
  FindingAddress,
  WaitingForFriend,
  Connecting,
  Connected,
  Failed,
  Closed,
};
enum class Fail {
  None,
  BadCode,
  NoSocket,
  NoGoBetween,
  FriendNotFound,
  CouldNotConnect,
  VersionMismatch,
};
enum class NatKind { Unknown, EndpointIndependent, Symmetric, NoReply };

struct ConnectionConfig {
  SessionConfig session;
  std::vector<std::string> stunServers;
  std::vector<std::string> trackers;
  std::vector<std::string> directPeers;
  bool useGoBetween = true;
  uint16_t localPort = 0;
  uint32_t findFriendTimeoutMs = 180000;
  uint32_t connectTimeoutMs = 25000;
  uint32_t announceIntervalMs = 4000;
};

struct ConnectionStatus {
  Phase phase = Phase::Idle;
  Fail fail = Fail::None;
  CloseReason closeReason = CloseReason::None;
  std::string text;
  std::string roomCode;
  std::string mappedMasked;
  NatKind nat = NatKind::Unknown;
  int candidates = 0;
  int trackersAnswering = 0;
  int stunAnswers = 0;
  std::string peerMasked;
  std::string detail;
  std::string diagnostics;
  bool low = false;
  std::vector<uint8_t> peerFingerprint;
  Issue issue = Issue::None;
  SessionStats stats;
};

class Connection {
 public:
  Connection();
  ~Connection();
  static ConnectionConfig defaultConfig();
  bool start(const std::string& code, const ConnectionConfig& cfg);
  ConnectionStatus status() const;
  bool send(const uint8_t* data, size_t size, bool reliable = true);
  bool send(const std::string& s, bool reliable = true) { return send((const uint8_t*)s.data(), s.size(), reliable); }
  bool receive(std::vector<uint8_t>& out, bool* reliable = nullptr);
  void close();

 private:
  void run(std::string code, ConnectionConfig cfg);
  struct Out {
    std::vector<uint8_t> data;
    bool reliable;
  };
  struct In {
    std::vector<uint8_t> data;
    bool reliable;
  };
  mutable std::mutex m_;
  ConnectionStatus st_;
  std::deque<Out> out_;
  size_t outBytes_ = 0;
  std::deque<In> in_;
  std::atomic<bool> stop_{false};
  std::thread th_;
};

const char* phaseName(Phase p);
const char* natKindName(NatKind k);
inline const char* ipWarningText() { return "Warning: playing online exposes your internet address (IP) to the other player."; }

}
