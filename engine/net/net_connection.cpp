#include "net_connection.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "net_proto.h"
#include "net_udp.h"

namespace xdnet {

static bool netLogOn() {
  static const bool on = std::getenv("XD_NET_LOG") != nullptr;
  return on;
}
static std::string shown(const Endpoint& e) {
  static const bool full = [] {
    const char* v = std::getenv("XD_NET_LOG");
    return v && v[0] == '2';
  }();
  return full ? e.str() : e.maskedStr();
}
#define NETLOG(...) \
  do { \
    if (netLogOn()) { \
      std::fprintf(stderr, "[net] " __VA_ARGS__); \
      std::fprintf(stderr, "\n"); \
    } \
  } while (0)

const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Idle: return "idle";
    case Phase::FindingAddress: return "finding address";
    case Phase::WaitingForFriend: return "waiting for friend";
    case Phase::Connecting: return "connecting";
    case Phase::Connected: return "connected";
    case Phase::Failed: return "failed";
    case Phase::Closed: return "closed";
  }
  return "?";
}

const char* natKindName(NatKind k) {
  switch (k) {
    case NatKind::EndpointIndependent: return "router keeps one outside port (good for connecting)";
    case NatKind::Symmetric: return "router uses a different outside port per destination (hard: VPN, mobile or strict network)";
    case NatKind::NoReply: return "no answer from the address helpers (UDP blocked, or offline)";
    default: return "unknown";
  }
}

ConnectionConfig Connection::defaultConfig() {
  ConnectionConfig c;
  c.stunServers = {"stun.l.google.com:19302", "stun.cloudflare.com:3478", "stun.nextcloud.com:3478",
                   "global.stun.twilio.com:3478", "stun1.l.google.com:19302"};
  c.trackers = {"tracker.opentrackr.org:1337", "open.demonii.com:1337", "tracker.torrent.eu.org:451",
                "tracker.dler.org:6969",       "open.stealth.si:80",    "exodus.desync.com:6969",
                "open.tracker.cl:1337",        "tracker.openbittorrent.com:6969"};
  return c;
}

Connection::Connection() = default;
Connection::~Connection() { close(); }

bool Connection::start(const std::string& typed, const ConnectionConfig& cfg) {
  std::string code = normalizeRoomCode(typed);
  std::lock_guard<std::mutex> lk(m_);
  if (th_.joinable() || st_.phase != Phase::Idle) return false;
  if (code.empty()) {
    st_.phase = Phase::Failed;
    st_.fail = Fail::BadCode;
    st_.text = "That room code is not valid. Check the letters and numbers.";
    return false;
  }
  st_.roomCode = formatRoomCode(code);
  st_.phase = Phase::FindingAddress;
  st_.text = "Starting...";
  th_ = std::thread([this, code, cfg] { run(code, cfg); });
  return true;
}

ConnectionStatus Connection::status() const {
  std::lock_guard<std::mutex> lk(m_);
  return st_;
}

bool Connection::send(const uint8_t* data, size_t size, bool reliable) {
  std::lock_guard<std::mutex> lk(m_);
  if (st_.phase != Phase::Connected || size > (reliable ? kMaxMessage : kMaxUnreliable)) return false;
  if (outBytes_ + size > (2u << 20)) return false;
  out_.push_back({std::vector<uint8_t>(data, data + size), reliable});
  outBytes_ += size;
  return true;
}

bool Connection::receive(std::vector<uint8_t>& out, bool* reliable) {
  std::lock_guard<std::mutex> lk(m_);
  if (in_.empty()) return false;
  out = std::move(in_.front().data);
  if (reliable) *reliable = in_.front().reliable;
  in_.pop_front();
  return true;
}

void Connection::close() {
  stop_ = true;
  if (th_.joinable()) th_.join();
}

namespace {
struct Tracker {
  std::string name;
  Endpoint ep;
  bool resolved = false;
  bool answered = false;
  uint64_t connId = 0;
  uint64_t connTime = 0;
  bool haveConn = false;
  uint32_t connTx = 0, annTx = 0;
  uint64_t lastCycle = 0;
  bool started = false;
};
struct Stun {
  std::string name;
  Endpoint ep;
  uint8_t tid[12];
  bool answered = false;
};
struct ResolveState {
  std::mutex m;
  std::vector<Endpoint> ep;
  std::vector<char> done, ok;
};
std::shared_ptr<ResolveState> resolveAll(const std::vector<std::string>& names, uint32_t timeoutMs, const std::atomic<bool>& stop) {
  auto st = std::make_shared<ResolveState>();
  st->ep.resize(names.size());
  st->done.assign(names.size(), 0);
  st->ok.assign(names.size(), 0);
  for (size_t i = 0; i < names.size(); i++) {
    std::thread([st, i, name = names[i]] {
      Endpoint e;
      bool ok = UdpSocket::resolveHostPort(name, e);
      std::lock_guard<std::mutex> lk(st->m);
      st->ep[i] = e;
      st->ok[i] = ok;
      st->done[i] = 1;
    }).detach();
  }
  uint64_t end = nowMs() + timeoutMs;
  while (nowMs() < end && !stop) {
    {
      std::lock_guard<std::mutex> lk(st->m);
      bool all = true;
      for (char d : st->done) all = all && d;
      if (all) break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return st;
}
bool plausiblePeer(const Endpoint& e) {
  return e.family == 4 && e.port != 0 && e.ip[0] != 0 && e.ip[0] != 127 && e.ip[0] < 224;
}
uint32_t rnd32() {
  uint32_t x;
  randomBytes(&x, 4);
  return x;
}
}

void Connection::run(std::string code, ConnectionConfig cfg) {
  auto setStatus = [&](auto&& fn) {
    std::lock_guard<std::mutex> lk(m_);
    fn(st_);
  };
  auto fail = [&](Fail f, const std::string& text) {
    setStatus([&](ConnectionStatus& s) {
      s.phase = Phase::Failed;
      s.fail = f;
      s.text = text;
    });
  };

  UdpSocket sock;
  if (!sock.open(cfg.localPort)) {
    fail(Fail::NoSocket, "This PC would not open a network port. Check the firewall or other programs.");
    return;
  }
  cfg.session.handshakeTimeoutMs = cfg.findFriendTimeoutMs + cfg.connectTimeoutMs + 5000;
  Session session(code, cfg.session, [&](const Endpoint& to, const uint8_t* d, size_t n) { sock.sendTo(to, d, n); });
  int candidates = 0;
  const uint64_t tStart = nowMs();
  int64_t tStun = -1, tTrk = -1, tCand = -1, tConn = -1;
  std::vector<Endpoint> candList;
  auto addCand = [&](const Endpoint& e) {
    for (auto& x : candList)
      if (x == e) return;
    candList.push_back(e);
    NETLOG("candidate %s (%d so far)", shown(e).c_str(), candidates + 1);
    if (tCand < 0) tCand = (int64_t)(nowMs() - tStart);
    session.addCandidate(e);
    candidates++;
  };

  std::vector<Stun> stuns;
  std::vector<Tracker> trackers;
  Endpoint mapped;
  NatKind nat = NatKind::Unknown;
  int stunAnswers = 0;
  std::vector<Endpoint> mappedAll;
  if (cfg.useGoBetween) {
    std::vector<std::string> all = cfg.stunServers;
    size_t nStun = all.size();
    all.insert(all.end(), cfg.trackers.begin(), cfg.trackers.end());
    auto rs = resolveAll(all, 3000, stop_);
    {
      std::lock_guard<std::mutex> lk(rs->m);
      for (size_t i = 0; i < nStun && stuns.size() < 4; i++) {
        if (!rs->ok[i]) continue;
        Stun s;
        s.name = all[i];
        s.ep = rs->ep[i];
        randomBytes(s.tid, 12);
        stuns.push_back(s);
      }
      for (size_t i = nStun; i < all.size(); i++) {
        Tracker t;
        t.name = all[i];
        t.resolved = rs->ok[i] != 0;
        t.ep = rs->ep[i];
        NETLOG("tracker %s %s", t.name.c_str(), t.resolved ? shown(t.ep).c_str() : "(no address)");
        trackers.push_back(t);
      }
    }
    for (auto& s : stuns) {
      auto req = stunBuildRequest(s.tid);
      sock.sendTo(s.ep, req.data(), req.size());
    }
  }

  uint8_t infohash[20] = {};
  uint8_t peerId[20];
  randomBytes(peerId, 20);
  uint32_t trackerKey = rnd32();

  uint64_t t0 = nowMs();
  uint64_t connectingSince = 0;
  bool stunDone = !cfg.useGoBetween;
  bool trackersReady = !cfg.useGoBetween;
  bool announcedStop = false;
  uint16_t announcePort = sock.localPort();
  Phase phase = Phase::FindingAddress;
  for (auto& d : cfg.directPeers) {
    Endpoint e;
    if (Endpoint::parse(d, e)) addCand(e);
  }
  if (candidates) {
    phase = Phase::Connecting;
    connectingSince = t0;
  }

  auto pump = [&](uint64_t now) {
    uint8_t buf[2048];
    Endpoint from;
    for (int k = 0; k < 512; k++) {
      int n = sock.recvFrom(from, buf, sizeof buf);
      if (n < 0) break;
      if (n == 0) continue;
      if (buf[0] >= 0xA1 && buf[0] <= 0xA3) {
        session.onDatagram(from, buf, (size_t)n, now);
      } else if (looksLikeStun((const uint8_t*)buf, (size_t)n)) {
        for (auto& s : stuns) {
          Endpoint m;
          if (!s.answered && from == s.ep && stunParseResponse(buf, (size_t)n, s.tid, m)) {
            s.answered = true;
            stunAnswers++;
            mappedAll.push_back(m);
            NETLOG("STUN %s says we are %s", s.name.c_str(), shown(m).c_str());
          }
        }
      } else {
        uint32_t tx;
        int act = trackerPeekAction(buf, (size_t)n, tx);
        if (act < 0) continue;
        for (auto& t : trackers) {
          if (!t.resolved || !(from == t.ep)) continue;
          if (act == 0 && tx == t.connTx && trackerParseConnect(buf, (size_t)n, tx, t.connId)) {
            t.haveConn = true;
            t.connTime = now;
            t.annTx = rnd32();
            auto a = trackerBuildAnnounce(t.connId, t.annTx, infohash, peerId, t.started ? kEvNone : kEvStarted,
                                          trackerKey, announcePort);
            t.started = true;
            sock.sendTo(t.ep, a.data(), a.size());
          } else if (act == 1 && tx == t.annTx) {
            uint32_t iv;
            std::vector<Endpoint> peers;
            if (trackerParseAnnounce(buf, (size_t)n, tx, iv, peers)) {
              t.answered = true;
              if (tTrk < 0) tTrk = (int64_t)(now - tStart);
              NETLOG("tracker %s answered: %zu peers, interval %us", t.name.c_str(), peers.size(), iv);
              for (auto& p : peers) {
                if (mapped.valid() && p.family == 4 && std::memcmp(p.ip, mapped.ip, 4) == 0 && p.port == announcePort) continue;
                if (plausiblePeer(p)) addCand(p);
              }
            }
          }
        }
      }
    }
  };

  auto sendStopToTrackers = [&] {
    if (announcedStop) return;
    announcedStop = true;
    for (auto& t : trackers) {
      if (!t.resolved || !t.haveConn) continue;
      auto a = trackerBuildAnnounce(t.connId, rnd32(), infohash, peerId, kEvStopped, trackerKey, announcePort);
      sock.sendTo(t.ep, a.data(), a.size());
    }
  };

  auto secs = [](int64_t t) -> std::string {
    if (t < 0) return "never";
    char b[32];
    std::snprintf(b, sizeof b, "%.1f s", (double)t / 1000.0);
    return b;
  };
  auto buildDiag = [&]() {
    std::string d;
    std::string stunOk, stunBad;
    for (auto& x : stuns) (x.answered ? stunOk : stunBad) += (x.answered ? stunOk : stunBad).empty() ? x.name : ", " + x.name;
    d += std::string("NAT: ") + natKindName(nat) + "\n";
    d += "STUN answered " + std::to_string(stunAnswers) + "/" + std::to_string(stuns.size()) + (stunOk.empty() ? "" : " (" + stunOk + ")") +
         (stunBad.empty() ? "" : "; silent: " + stunBad) + "\n";
    std::string trkOk, trkBad, trkNone;
    int ta = 0;
    for (auto& t : trackers) {
      std::string& dst = t.answered ? trkOk : (t.resolved ? trkBad : trkNone);
      dst += dst.empty() ? t.name : ", " + t.name;
      ta += t.answered;
    }
    d += "Trackers answered " + std::to_string(ta) + "/" + std::to_string(trackers.size()) + (trkOk.empty() ? "" : " (" + trkOk + ")") +
         (trkBad.empty() ? "" : "; silent: " + trkBad) + (trkNone.empty() ? "" : "; name not found: " + trkNone) + "\n";
    const SessionStats& ss = session.stats();
    d += "Candidates: " + std::to_string(candidates) + "; handshake: hello sent " + std::to_string(ss.helloSent) + ", valid hello received " +
         std::to_string(ss.helloReceived) + ", key failures " + std::to_string(ss.authFailures) + ", blocked addresses " +
         std::to_string(ss.blockedAddresses) + ", rejected datagrams " + std::to_string(ss.rejected) + ", friend's version differs: " +
         (session.issue() == Issue::PeerVersionDiffers ? "yes" : "no") + "\n";
    d += "Timings: STUN done " + secs(tStun) + ", first tracker answer " + secs(tTrk) + ", first friend address " + secs(tCand) +
         ", connected " + secs(tConn) + "; go-between " + (cfg.useGoBetween ? "on" : "off") + ", direct addresses " +
         std::to_string(cfg.directPeers.size()) + "\n";
    return d;
  };
  uint64_t lastDiag = 0;
  std::string diag;
  bool connectedShown = false;
  while (!stop_) {
    uint64_t now = nowMs();
    pump(now);

    if (!stunDone && (stunAnswers == (int)stuns.size() || now - t0 > 2500)) {
      stunDone = true;
      tStun = (int64_t)(now - tStart);
      if (!mappedAll.empty()) {
        mapped = mappedAll[0];
        nat = NatKind::EndpointIndependent;
        for (auto& m : mappedAll)
          if (m.port != mapped.port) nat = NatKind::Symmetric;
        announcePort = mapped.port;
      } else {
        nat = NatKind::NoReply;
      }
    }
    if (stunDone && !trackersReady) {
      trackersReady = true;
      roomInfohash(code, infohash);
      if (phase == Phase::FindingAddress) phase = Phase::WaitingForFriend;
    }

    if (trackersReady && cfg.useGoBetween && session.state() == SessionState::Handshaking) {
      for (auto& t : trackers) {
        if (!t.resolved) continue;
        if (t.lastCycle != 0 && now - t.lastCycle < cfg.announceIntervalMs) continue;
        t.lastCycle = now;
        if (t.haveConn && now - t.connTime < 55000) {
          t.annTx = rnd32();
          auto a = trackerBuildAnnounce(t.connId, t.annTx, infohash, peerId, t.started ? kEvNone : kEvStarted,
                                        trackerKey, announcePort);
          t.started = true;
          sock.sendTo(t.ep, a.data(), a.size());
        } else {
          t.haveConn = false;
          t.connTx = rnd32();
          auto c = trackerBuildConnect(t.connTx);
          sock.sendTo(t.ep, c.data(), c.size());
        }
      }
    }

    {
      std::lock_guard<std::mutex> lk(m_);
      while (!out_.empty() && session.state() == SessionState::Connected) {
        bool ok = out_.front().reliable ? session.sendReliable(out_.front().data.data(), out_.front().data.size())
                                        : session.sendUnreliable(out_.front().data.data(), out_.front().data.size());
        if (!ok && out_.front().reliable) break;
        outBytes_ -= std::min(outBytes_, out_.front().data.size());
        out_.pop_front();
      }
    }
    session.update(now);
    {
      std::lock_guard<std::mutex> lk(m_);
      std::vector<uint8_t> msg;
      bool rel;
      while (in_.size() < 4096 && session.receive(msg, &rel)) in_.push_back({std::move(msg), rel});
    }

    if (candidates > 0 && phase == Phase::WaitingForFriend) {
      phase = Phase::Connecting;
      connectingSince = now;
    }
    if (phase == Phase::FindingAddress && candidates > 0) {
      phase = Phase::Connecting;
      connectingSince = now;
    }

    bool finished = false;
    Phase newPhase = phase;
    Fail f = Fail::None;
    std::string text;
    if (session.state() == SessionState::Connected) {
      newPhase = Phase::Connected;
      if (!connectedShown) {
        connectedShown = true;
        tConn = (int64_t)(now - tStart);
        std::fprintf(stderr, "[net] connected after %s\n%s", secs(tConn).c_str(), buildDiag().c_str());
        sendStopToTrackers();
      }
    } else if (session.state() == SessionState::Closed) {
      newPhase = Phase::Closed;
      finished = true;
    } else if (phase == Phase::WaitingForFriend && now - t0 > cfg.findFriendTimeoutMs) {
      int answering = 0;
      for (auto& t : trackers) answering += t.answered;
      newPhase = Phase::Failed;
      finished = true;
      if (cfg.useGoBetween && answering == 0) {
        f = Fail::NoGoBetween;
        text = "Could not reach the public matchmaking servers. Check the internet connection or a firewall.";
      } else {
        f = Fail::FriendNotFound;
        text = "Nobody else showed up with this room code. Check that your friend typed the same code, and that you both started at about the same time.";
      }
    } else if (phase == Phase::Connecting && now - connectingSince > cfg.connectTimeoutMs) {
      newPhase = Phase::Failed;
      finished = true;
      if (session.issue() == Issue::PeerVersionDiffers) {
        f = Fail::VersionMismatch;
        text = "Your friend has a different online version. Both need the same version of the game.";
      } else {
        f = Fail::CouldNotConnect;
        text = "Found your friend but could not open a direct connection. One of your networks (router, VPN or mobile "
               "carrier) blocks it. Try turning a VPN off, or another network such as a phone hotspot.";
      }
    }
    phase = newPhase == Phase::Failed || newPhase == Phase::Closed ? phase : newPhase;

    {
      std::lock_guard<std::mutex> lk(m_);
      st_.phase = newPhase;
      st_.mappedMasked = mapped.valid() ? mapped.maskedStr() : "";
      st_.nat = nat;
      st_.candidates = candidates;
      int ta = 0;
      for (auto& t : trackers) ta += t.answered;
      st_.trackersAnswering = ta;
      st_.stunAnswers = stunAnswers;
      st_.peerMasked = session.peer().valid() ? session.peer().maskedStr() : "";
      st_.low = session.isLowSide();
      st_.peerFingerprint = session.peerFingerprint();
      st_.issue = session.issue();
      st_.stats = session.stats();
      st_.closeReason = session.closeReason();
      st_.fail = f;
      if (finished || now - lastDiag > 500) {
        lastDiag = now;
        diag = buildDiag();
      }
      st_.diagnostics = diag;
      st_.detail = newPhase == Phase::Failed || newPhase == Phase::Closed
                       ? std::string("NAT: ") + natKindName(nat) + "; helper servers answered: " + std::to_string(stunAnswers) + " address, " +
                             std::to_string(ta) + " matchmaking"
                       : "";
      switch (newPhase) {
        case Phase::FindingAddress: st_.text = "Finding out how the internet sees this PC..."; break;
        case Phase::WaitingForFriend: st_.text = "Waiting for your friend to join this room code..."; break;
        case Phase::Connecting: st_.text = "Found your friend, opening the connection..."; break;
        case Phase::Connected: st_.text = "Connected."; break;
        case Phase::Closed:
          switch (session.closeReason()) {
            case CloseReason::Local: st_.text = "Disconnected."; break;
            case CloseReason::Remote: st_.text = "Your friend left."; break;
            case CloseReason::Timeout: st_.text = "The connection was lost."; break;
            case CloseReason::ProtocolError: st_.text = "The connection was closed: your friend sent something invalid."; break;
            default: st_.text = "Closed."; break;
          }
          break;
        case Phase::Failed: st_.text = text; break;
        default: break;
      }
    }
    if (finished) {
      std::fprintf(stderr, "[net] ended (%s): %s\n%s", phaseName(newPhase), st_.text.c_str(), diag.c_str());
      break;
    }
    sock.waitReadable(3);
  }

  if (session.state() == SessionState::Connected) {
    session.close();
    setStatus([&](ConnectionStatus& s) {
      s.phase = Phase::Closed;
      s.closeReason = CloseReason::Local;
      s.text = "Disconnected.";
    });
  } else if (stop_) {
    setStatus([&](ConnectionStatus& s) {
      if (s.phase != Phase::Failed && s.phase != Phase::Closed) {
        s.phase = Phase::Closed;
        s.closeReason = CloseReason::Local;
        s.text = "Cancelled.";
      }
    });
  }
  sendStopToTrackers();
}

}
