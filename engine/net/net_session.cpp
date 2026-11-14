#include "net_session.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace xdnet {

namespace {
constexpr uint8_t kHello = 0xA1, kConfirm = 0xA2, kData = 0xA3;
constexpr uint8_t kFrReliable = 1, kFrUnreliable = 2, kFrPing = 3, kFrPong = 4, kFrClose = 5;
constexpr size_t kChunk = 900;
constexpr size_t kMaxPlain = 1100;
constexpr uint32_t kSendWindow = 64;
constexpr uint32_t kRecvWindow = 256;
constexpr size_t kMaxCands = 16;
constexpr uint64_t kMaxCounter = 1ull << 62;

void put16(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back((uint8_t)(x >> 8));
  v.push_back((uint8_t)x);
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 3; i >= 0; i--) v.push_back((uint8_t)(x >> (8 * i)));
}
void put64(std::vector<uint8_t>& v, uint64_t x) {
  for (int i = 7; i >= 0; i--) v.push_back((uint8_t)(x >> (8 * i)));
}
uint32_t get16(const uint8_t* p) { return (uint32_t)p[0] << 8 | p[1]; }
uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint64_t get64(const uint8_t* p) { return (uint64_t)get32(p) << 32 | get32(p + 4); }
bool seqLess(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }
}

Session::Session(const std::string& code, const SessionConfig& cfg, SendFn send)
    : cfg_(cfg), send_(std::move(send)), code_(code), pake_(code, cfg.version) {
  if (cfg_.fingerprint.size() > kMaxFingerprint) cfg_.fingerprint.resize(kMaxFingerprint);
}

Session::~Session() = default;

void Session::addCandidate(const Endpoint& e) {
  if (!e.valid() || findCand(e) || cands_.size() >= kMaxCands) return;
  Cand c;
  c.ep = e;
  c.listed = true;
  cands_.push_back(c);
}

Session::Cand* Session::findCand(const Endpoint& e) {
  for (auto& c : cands_)
    if (c.ep == e) return &c;
  return nullptr;
}

void Session::sendHello(Cand& c, uint64_t now) {
  uint8_t p[kHelloSize];
  std::memset(p, 0, sizeof p);
  p[0] = kHello;
  p[1] = (uint8_t)(cfg_.version >> 8);
  p[2] = (uint8_t)cfg_.version;
  std::memcpy(p + 3, pake_.publicValue(), 32);
  c.lastHello = now;
  stats_.helloSent++;
  send_(c.ep, p, sizeof p);
}

void Session::sendConfirm(const Endpoint& to) {
  Cand* c = findCand(to);
  if (!c || !c->keyed) return;
  std::vector<uint8_t> plain;
  put16(plain, (uint32_t)cfg_.fingerprint.size());
  plain.insert(plain.end(), cfg_.fingerprint.begin(), cfg_.fingerprint.end());
  plain.push_back('O');
  plain.push_back('K');
  uint8_t ad[33];
  ad[0] = kConfirm;
  std::memcpy(ad + 1, c->keys.transcript, 32);
  std::vector<uint8_t> pkt(1 + plain.size() + 16);
  pkt[0] = kConfirm;
  aeadSeal(c->keys.sendKey, 0, ad, sizeof ad, plain.data(), plain.size(), pkt.data() + 1);
  lastConfirmSent_ = now_;
  send_(to, pkt.data(), pkt.size());
}

void Session::onDatagram(const Endpoint& from, const uint8_t* d, size_t n, uint64_t now) {
  now_ = now;
  if (state_ == SessionState::Closed) return;
  if (!started_) {
    started_ = true;
    startedAt_ = now;
  }
  if (n == 0 || n > kMaxDatagram) {
    stats_.rejected++;
    return;
  }
  if (now - rateWindowStart_ >= 1000) {
    rateWindowStart_ = now;
    rateCount_ = 0;
  }
  if (++rateCount_ > cfg_.maxPacketsPerSec) {
    stats_.rejected++;
    return;
  }
  switch (d[0]) {
    case kHello: onHello(from, d, n, now); break;
    case kConfirm: onConfirm(from, d, n, now); break;
    case kData: onData(from, d, n, now); break;
    default: stats_.rejected++; break;
  }
}

void Session::onHello(const Endpoint& from, const uint8_t* d, size_t n, uint64_t now) {
  if (n != kHelloSize) {
    stats_.rejected++;
    return;
  }
  uint16_t ver = (uint16_t)get16(d + 1);
  if (ver != cfg_.version) {
    issue_ = Issue::PeerVersionDiffers;
    return;
  }
  if (peerConfirmed_ && from != peer_) return;
  Cand* c = findCand(from);
  if (!c) {
    if (cands_.size() >= kMaxCands || stats_.authFailures >= cfg_.maxAuthFailures) return;
    Cand nc;
    nc.ep = from;
    cands_.push_back(nc);
    c = findCand(from);
  }
  if (c->blockedUntil) {
    if (now < c->blockedUntil) return;
    c->blockedUntil = 0;
    c->fails = 0;
  }
  stats_.helloReceived++;
  bool same = c->keyed && std::memcmp(c->theirY, d + 3, 32) == 0;
  if (!same) {
    if (c->lastReply && now - c->lastReply < 100) return;
    SessionKeys k;
    if (!pake_.finish(d + 3, k)) {
      stats_.rejected++;
      return;
    }
    std::memcpy(c->theirY, d + 3, 32);
    c->keys = k;
    c->keyed = true;
    weAreLow_ = std::memcmp(pake_.publicValue(), d + 3, 32) < 0;
  } else if (c->lastReply && now - c->lastReply < 100) {
    return;
  }
  c->lastReply = now;
  sendConfirm(from);
}

void Session::onConfirm(const Endpoint& from, const uint8_t* d, size_t n, uint64_t now) {
  if (n < 1 + 4 + 16 || n > kHelloSize) {
    stats_.rejected++;
    return;
  }
  Cand* c = findCand(from);
  if (!c || !c->keyed || now < c->blockedUntil) {
    stats_.rejected++;
    return;
  }
  if (peerConfirmed_) return;
  uint8_t ad[33];
  ad[0] = kConfirm;
  std::memcpy(ad + 1, c->keys.transcript, 32);
  std::vector<uint8_t> plain(n - 1 - 16);
  if (!aeadOpen(c->keys.recvKey, 0, ad, sizeof ad, d + 1, n - 1, plain.data())) {
    issue_ = Issue::WrongCodeOrTampered;
    stats_.rejected++;
    stats_.authFailures++;
    if (++c->fails >= 3 && !c->blockedUntil) {
      c->blockedUntil = now + cfg_.blockMs;
      stats_.blockedAddresses++;
    }
    return;
  }
  if (plain.size() < 4) return;
  size_t fl = get16(plain.data());
  if (fl > kMaxFingerprint || 2 + fl + 2 != plain.size() || plain[2 + fl] != 'O' || plain[3 + fl] != 'K') {
    stats_.rejected++;
    return;
  }
  peerFingerprint_.assign(plain.begin() + 2, plain.begin() + 2 + fl);
  peerConfirmed_ = true;
  active_ = (int)(c - cands_.data());
  peer_ = from;
  lastAuthRx_ = now;
  lastPing_ = 0;
  state_ = SessionState::Connected;
  issue_ = Issue::None;
  sendConfirm(from);
}

void Session::onData(const Endpoint& from, const uint8_t* d, size_t n, uint64_t now) {
  if (state_ != SessionState::Connected || active_ < 0) {
    stats_.rejected++;
    return;
  }
  if (n < 1 + 8 + 16 + 8) {
    stats_.rejected++;
    return;
  }
  uint64_t ctr = 0;
  for (int i = 0; i < 8; i++) ctr |= (uint64_t)d[1 + i] << (8 * i);
  if (ctr == 0 || ctr >= kMaxCounter) {
    stats_.rejected++;
    return;
  }
  if (ctr <= recvHigh_) {
    uint64_t back = recvHigh_ - ctr;
    if (back >= 64 || (recvMask_ >> back & 1)) {
      stats_.rejected++;
      return;
    }
  }
  std::vector<uint8_t> plain(n - 9 - 16);
  if (!aeadOpen(cands_[active_].keys.recvKey, ctr, d, 9, d + 9, n - 9, plain.data())) {
    stats_.rejected++;
    return;
  }
  if (ctr > recvHigh_) {
    uint64_t shift = ctr - recvHigh_;
    recvMask_ = shift >= 64 ? 0 : recvMask_ << shift;
    recvMask_ |= 1;
    if (recvHigh_ != 0 && shift > 1) stats_.packetsLost += shift - 1;
    recvHigh_ = ctr;
  } else {
    recvMask_ |= 1ull << (recvHigh_ - ctr);
    if (stats_.packetsLost) stats_.packetsLost--;
  }
  stats_.packetsReceived++;
  stats_.bytesReceived += n;
  lastAuthRx_ = now;
  peerSeenData_ = true;
  if (from != peer_) peer_ = from;
  uint32_t ack = get32(plain.data());
  uint32_t sack = get32(plain.data() + 4);
  for (auto& f : out_) {
    if (seqLess(f.seq, ack)) f.acked = true;
    else {
      uint32_t off = f.seq - ack;
      if (off >= 1 && off <= 32 && (sack >> (off - 1) & 1)) f.acked = true;
    }
  }
  {
    uint32_t ackedAbove = 0;
    for (auto it = out_.rbegin(); it != out_.rend(); ++it) {
      if (it->acked) ackedAbove++;
      else if (ackedAbove >= 3 && it->tx > 0 && now - it->sentAt >= std::max<uint32_t>(rto_ / 4, 20)) it->fast = true;
    }
  }
  while (!out_.empty() && out_.front().acked) {
    queuedBytes_ -= std::min(queuedBytes_, out_.front().data.size());
    out_.pop_front();
  }
  parseFrames(plain.data() + 8, plain.size() - 8, now);
}

void Session::parseFrames(const uint8_t* p, size_t n, uint64_t now) {
  size_t i = 0;
  while (i < n && state_ == SessionState::Connected) {
    uint8_t t = p[i++];
    if (t == kFrReliable) {
      if (n - i < 7) return;
      uint32_t seq = get32(p + i);
      bool more = p[i + 4] != 0;
      size_t len = get16(p + i + 5);
      i += 7;
      if (len > kChunk || n - i < len) return;
      ackNeeded_ = true;
      if (!seqLess(seq, expectSeq_) && seq - expectSeq_ < kRecvWindow && !inWindow_.count(seq)) {
        InFrame f;
        f.more = more;
        f.data.assign(p + i, p + i + len);
        inWindow_[seq] = std::move(f);
      }
      i += len;
    } else if (t == kFrUnreliable) {
      if (n - i < 2) return;
      size_t len = get16(p + i);
      i += 2;
      if (len > kMaxUnreliable || n - i < len) return;
      if (inbox_.size() < cfg_.maxInboxMessages) inbox_.push_back({std::vector<uint8_t>(p + i, p + i + len), false});
      i += len;
    } else if (t == kFrPing) {
      if (n - i < 8) return;
      if (pongsToSend_.size() < 8) pongsToSend_.push_back(get64(p + i));
      ackNeeded_ = true;
      i += 8;
    } else if (t == kFrPong) {
      if (n - i < 8) return;
      uint64_t sent = get64(p + i);
      i += 8;
      if (sent <= now && now - sent < 60000) {
        double rtt = (double)(now - sent);
        if (!haveRtt_) {
          srtt_ = rtt;
          rttvar_ = rtt / 2;
          haveRtt_ = true;
        } else {
          rttvar_ = 0.75 * rttvar_ + 0.25 * std::fabs(srtt_ - rtt);
          srtt_ = 0.875 * srtt_ + 0.125 * rtt;
          double dev = std::fabs(rtt - (double)stats_.rttLastMs);
          stats_.jitterMs = (uint32_t)((double)stats_.jitterMs + (dev - (double)stats_.jitterMs) / 16.0 + 0.5);
        }
        stats_.rttLastMs = (uint32_t)rtt;
        stats_.rttMs = (uint32_t)(srtt_ + 0.5);
        rto_ = (uint32_t)std::min(2000.0, std::max(100.0, srtt_ + 4 * rttvar_));
      }
    } else if (t == kFrClose) {
      closeWith(CloseReason::Remote);
      return;
    } else {
      return;
    }
  }
  if (!deliverReliable()) closeWith(CloseReason::ProtocolError);
}

bool Session::deliverReliable() {
  while (inbox_.size() < cfg_.maxInboxMessages) {
    auto it = inWindow_.find(expectSeq_);
    if (it == inWindow_.end()) break;
    partial_.insert(partial_.end(), it->second.data.begin(), it->second.data.end());
    bool more = it->second.more;
    inWindow_.erase(it);
    expectSeq_++;
    if (partial_.size() > kMaxMessage) return false;
    if (!more) {
      inbox_.push_back({std::move(partial_), true});
      partial_.clear();
    }
  }
  return true;
}

bool Session::sendReliable(const uint8_t* data, size_t size) {
  if (state_ != SessionState::Connected || size > kMaxMessage) return false;
  if (queuedBytes_ + size > cfg_.maxQueuedBytes) return false;
  size_t off = 0;
  do {
    size_t len = std::min(kChunk, size - off);
    OutFrame f;
    f.seq = nextSeq_++;
    f.more = off + len < size;
    f.data.assign(data + off, data + off + len);
    out_.push_back(std::move(f));
    off += len;
  } while (off < size);
  queuedBytes_ += size;
  return true;
}

bool Session::sendUnreliable(const uint8_t* data, size_t size) {
  if (state_ != SessionState::Connected || size > kMaxUnreliable) return false;
  if (unreliableOut_.size() >= 256) unreliableOut_.pop_front();
  unreliableOut_.emplace_back(data, data + size);
  return true;
}

bool Session::receive(std::vector<uint8_t>& out, bool* reliable) {
  if (inbox_.empty()) return false;
  out = std::move(inbox_.front().data);
  if (reliable) *reliable = inbox_.front().reliable;
  inbox_.pop_front();
  deliverReliable();
  return true;
}

void Session::sendPacket(const std::vector<uint8_t>& plain) {
  if (active_ < 0) return;
  std::vector<uint8_t> pkt(9 + plain.size() + 16);
  pkt[0] = kData;
  uint64_t ctr = sendCtr_++;
  for (int i = 0; i < 8; i++) pkt[1 + i] = (uint8_t)(ctr >> (8 * i));
  aeadSeal(cands_[active_].keys.sendKey, ctr, pkt.data(), 9, plain.data(), plain.size(), pkt.data() + 9);
  stats_.packetsSent++;
  stats_.bytesSent += pkt.size();
  lastSend_ = now_;
  send_(peer_, pkt.data(), pkt.size());
}

void Session::flush(uint64_t now, bool force) {
  bool pingDue = lastPing_ == 0 || now - lastPing_ >= cfg_.pingIntervalMs;
  for (int packets = 0; packets < 8; packets++) {
    std::vector<uint8_t> p;
    put32(p, expectSeq_);
    uint32_t sack = 0;
    for (uint32_t i = 0; i < 32; i++)
      if (inWindow_.count(expectSeq_ + 1 + i)) sack |= 1u << i;
    put32(p, sack);
    size_t headerSize = p.size();
    while (!pongsToSend_.empty() && p.size() + 9 <= kMaxPlain) {
      p.push_back(kFrPong);
      put64(p, pongsToSend_.back());
      pongsToSend_.pop_back();
    }
    if (pingDue && p.size() + 9 <= kMaxPlain) {
      p.push_back(kFrPing);
      put64(p, now);
      lastPing_ = now;
      pingDue = false;
    }
    while (!unreliableOut_.empty() && p.size() + 3 + unreliableOut_.front().size() <= kMaxPlain) {
      p.push_back(kFrUnreliable);
      put16(p, (uint32_t)unreliableOut_.front().size());
      p.insert(p.end(), unreliableOut_.front().begin(), unreliableOut_.front().end());
      unreliableOut_.pop_front();
    }
    uint32_t idx = 0;
    bool moreWaiting = false;
    for (auto& f : out_) {
      if (idx++ >= kSendWindow) break;
      if (f.acked) continue;
      bool due = f.tx == 0 || f.fast || now - f.sentAt >= (uint64_t)rto_ << std::min<uint32_t>(f.tx - 1, 4);
      if (!due) continue;
      if (p.size() + 8 + f.data.size() > kMaxPlain) {
        moreWaiting = true;
        break;
      }
      p.push_back(kFrReliable);
      put32(p, f.seq);
      p.push_back(f.more ? 1 : 0);
      put16(p, (uint32_t)f.data.size());
      p.insert(p.end(), f.data.begin(), f.data.end());
      if (f.tx > 0) stats_.retransmits++;
      f.tx++;
      f.fast = false;
      f.sentAt = now;
    }
    if (p.size() > headerSize || ackNeeded_ || force) {
      ackNeeded_ = false;
      force = false;
      sendPacket(p);
    }
    if (!moreWaiting && unreliableOut_.empty() && pongsToSend_.empty()) break;
  }
}

void Session::update(uint64_t now) {
  now_ = now;
  if (state_ == SessionState::Closed) return;
  if (!started_) {
    started_ = true;
    startedAt_ = now;
  }
  if (state_ == SessionState::Handshaking) {
    if (now - startedAt_ > cfg_.handshakeTimeoutMs) {
      closeWith(CloseReason::HandshakeTimeout);
      return;
    }
    for (auto& c : cands_) {
      if (now < c.blockedUntil) continue;
      if (c.lastHello == 0 || now - c.lastHello >= cfg_.helloIntervalMs) {
        sendHello(c, now);
        if (c.keyed) sendConfirm(c.ep);
      }
    }
    return;
  }
  if (now - lastAuthRx_ > cfg_.timeoutMs) {
    closeWith(CloseReason::Timeout);
    return;
  }
  if (!peerSeenData_ && now - lastConfirmSent_ >= cfg_.helloIntervalMs) sendConfirm(peer_);
  flush(now, false);
}

void Session::close() {
  if (state_ == SessionState::Connected) {
    std::vector<uint8_t> p;
    put32(p, expectSeq_);
    put32(p, 0);
    p.push_back(kFrClose);
    p.push_back(0);
    for (int i = 0; i < 3; i++) sendPacket(p);
  }
  closeWith(CloseReason::Local);
}

void Session::closeWith(CloseReason r) {
  if (state_ == SessionState::Closed) return;
  state_ = SessionState::Closed;
  closeReason_ = r;
}

}
