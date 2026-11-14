#include "net_trade.h"

#include <cstring>

namespace xdnet::trade {

namespace {

enum Type : uint8_t { kOffer = 1, kVerdict = 2, kReady = 3, kConfirm = 4, kAck = 5, kCancel = 6 };
constexpr uint64_t kCommitTimeoutMs = 15000;

void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; i++) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
void put64(std::vector<uint8_t>& v, uint64_t x) {
  for (int i = 0; i < 8; i++) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
struct Reader {
  const std::vector<uint8_t>& v;
  size_t pos;
  bool bad = false;
  uint32_t u8() { if (pos + 1 > v.size()) { bad = true; return 0; } return v[pos++]; }
  uint32_t u16() { uint32_t a = u8(); return a | (u8() << 8); }
  uint32_t u32() { uint32_t a = u16(); return a | (u16() << 16); }
  uint64_t u64() { uint64_t a = u32(); return a | (static_cast<uint64_t>(u32()) << 32); }
  std::string rest() { std::string s(v.begin() + static_cast<long>(pos), v.end()); pos = v.size(); return s; }
};
uint64_t fnv(uint64_t h, const std::vector<uint8_t>& d) {
  for (uint8_t b : d) { h ^= b; h *= 1099511628211ULL; }
  uint64_t n = d.size();
  h ^= n; h *= 1099511628211ULL;
  return h;
}
std::string clip(const std::string& s) { return s.size() > 300 ? s.substr(0, 300) : s; }

}

TradeSession::TradeSession(bool low, Hooks hooks) : low_(low), hooks_(std::move(hooks)) {}

void TradeSession::send(uint8_t type, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> m;
  m.push_back(type);
  m.insert(m.end(), payload.begin(), payload.end());
  out_.push_back(std::move(m));
}

std::vector<std::vector<uint8_t>> TradeSession::takeOutgoing() {
  std::vector<std::vector<uint8_t>> r;
  r.swap(out_);
  return r;
}

uint64_t TradeSession::digest() const {
  uint64_t h = 1469598103934665603ULL;
  h = fnv(h, low_ ? mine_.record : theirs_.record);
  h = fnv(h, low_ ? theirs_.record : mine_.record);
  return h;
}

bool TradeSession::iAmReady() const {
  return mine_.seq != 0 && theirs_.seq != 0 && myReady_.own == mine_.seq && myReady_.other == theirs_.seq;
}
bool TradeSession::friendIsReady() const {
  return mine_.seq != 0 && theirs_.seq != 0 && peerReady_.own == theirs_.seq && peerReady_.other == mine_.seq;
}
bool TradeSession::canReady() const {
  return !mine_.record.empty() && !theirs_.record.empty() && theirsVerdict_.ok && myOfferJudged() && friendVerdict_.ok;
}

bool TradeSession::offer(const std::vector<uint8_t>& record) {
  if (state_ != State::Negotiating && state_ != State::Confirming) { why_ = "The trade is already being finished."; return false; }
  if (record.empty() || record.size() > kMaxRecord) { why_ = "That Pokemon can't be sent."; return false; }
  mine_.seq = nextSeq_++;
  mine_.record = record;
  friendVerdictSeq_ = 0;
  friendVerdict_ = Verdict{};
  myYes_ = false;
  std::vector<uint8_t> p;
  put32(p, mine_.seq);
  p.push_back(static_cast<uint8_t>(record.size() & 0xFF));
  p.push_back(static_cast<uint8_t>(record.size() >> 8));
  p.insert(p.end(), record.begin(), record.end());
  send(kOffer, p);
  advance();
  return true;
}

bool TradeSession::withdraw() {
  if (state_ != State::Negotiating && state_ != State::Confirming) { why_ = "The trade is already being finished."; return false; }
  if (mine_.record.empty()) return true;
  mine_.seq = nextSeq_++;
  mine_.record.clear();
  friendVerdictSeq_ = 0;
  myYes_ = false;
  std::vector<uint8_t> p;
  put32(p, mine_.seq);
  p.push_back(0);
  p.push_back(0);
  send(kOffer, p);
  advance();
  return true;
}

void TradeSession::sendReady(bool ready) {
  std::vector<uint8_t> p;
  put32(p, ready ? mine_.seq : 0);
  put32(p, ready ? theirs_.seq : 0);
  p.push_back(ready ? 1 : 0);
  send(kReady, p);
}

bool TradeSession::setReady(bool ready) {
  if (state_ != State::Negotiating && state_ != State::Confirming) { why_ = "The trade is already being finished."; return false; }
  if (ready) {
    if (!canReady()) {
      if (mine_.record.empty()) why_ = "Choose one of your Pokemon first.";
      else if (theirs_.record.empty()) why_ = "Your friend has not offered a Pokemon yet.";
      else if (!theirsVerdict_.ok) why_ = theirsVerdict_.text;
      else if (!myOfferJudged()) why_ = "Waiting for your friend's game to check your Pokemon.";
      else why_ = friendVerdict_.text;
      return false;
    }
    myReady_ = {mine_.seq, theirs_.seq};
  } else {
    myReady_ = {};
  }
  myYes_ = false;
  sendReady(ready);
  advance();
  return true;
}

bool TradeSession::confirm(bool yes) {
  if (state_ != State::Confirming) { why_ = "Both of you have to be Ready first."; return false; }
  if (!yes) return setReady(false);
  if (myYes_) return true;
  myYes_ = true;
  std::vector<uint8_t> p;
  put32(p, mine_.seq);
  put32(p, theirs_.seq);
  put64(p, digest());
  p.push_back(1);
  send(kConfirm, p);
  advance();
  return true;
}

void TradeSession::cancel() {
  if (state_ == State::Done || state_ == State::Ended || state_ == State::Committing) return;
  send(kCancel, {});
  end(Outcome::NothingChanged, "You left the trade. Nothing was changed.");
}

void TradeSession::end(Outcome o, const std::string& text) {
  if (state_ == State::Done || state_ == State::Ended) return;
  state_ = (o == Outcome::Traded) ? State::Done : State::Ended;
  outcome_ = o;
  endText_ = text;
}

void TradeSession::advance() {
  for (int guard = 0; guard < 4; guard++) {
    if (state_ == State::Negotiating) {
      if (iAmReady() && friendIsReady()) { state_ = State::Confirming; myYes_ = false; continue; }
      return;
    }
    if (state_ == State::Confirming) {
      if (!(iAmReady() && friendIsReady())) { state_ = State::Negotiating; myYes_ = false; peerYes_ = false; continue; }
      const bool peerOk = peerYes_ && peerYesPair_.own == theirs_.seq && peerYesPair_.other == mine_.seq;
      if (myYes_ && peerOk) {
        if (peerYesDigest_ != digest()) {
          send(kCancel, {});
          end(Outcome::NothingChanged, "The two games disagree about what is being traded. Nothing was changed.");
          return;
        }
        Verdict v = hooks_.prepare ? hooks_.prepare(mine_.record, theirs_.record) : Verdict{};
        if (!v.ok) {
          std::vector<uint8_t> p(v.text.begin(), v.text.end());
          send(kCancel, p);
          end(Outcome::NothingChanged, "The trade can't be finished on this PC: " + v.text + " Nothing was changed.");
          return;
        }
        std::vector<uint8_t> p;
        put64(p, digest());
        send(kAck, p);
        sentAck_ = true;
        state_ = State::Committing;
        lastProgressMs_ = nowMs_;
        continue;
      }
      return;
    }
    if (state_ == State::Committing) {
      if (sentAck_ && peerAck_) {
        Verdict v = hooks_.commit ? hooks_.commit(mine_.record, theirs_.record) : Verdict{};
        if (v.ok) end(Outcome::Traded, "The trade is done.");
        else end(Outcome::CommitFailed, "The trade was agreed but saving failed on this PC: " + v.text +
                                         " Your friend's game may have saved; check your Pokemon.");
      }
      return;
    }
    return;
  }
}

void TradeSession::onMessage(const std::vector<uint8_t>& m) {
  if (state_ == State::Done || state_ == State::Ended || m.empty()) return;
  Reader r{m, 1};
  switch (m[0]) {
    case kOffer: {
      uint32_t seq = r.u32();
      uint32_t len = r.u16();
      if (r.bad || len > kMaxRecord || r.pos + len != m.size() || seq == 0) return;
      if (state_ == State::Committing && !peerAck_) { end(Outcome::NothingChanged, "Your friend changed their offer. Nothing was changed."); return; }
      if (state_ == State::Committing) return;
      theirs_.seq = seq;
      theirs_.record.assign(m.begin() + 7, m.end());
      myYes_ = false;
      peerYes_ = false;
      if (theirs_.record.empty()) {
        theirsVerdict_ = Verdict{false, "Your friend has not offered a Pokemon."};
      } else {
        theirsVerdict_ = hooks_.check ? hooks_.check(theirs_.record) : Verdict{};
      }
      std::vector<uint8_t> p;
      put32(p, seq);
      p.push_back(theirsVerdict_.ok ? 1 : 0);
      std::string t = clip(theirsVerdict_.text);
      p.insert(p.end(), t.begin(), t.end());
      send(kVerdict, p);
      break;
    }
    case kVerdict: {
      uint32_t seq = r.u32();
      uint32_t ok = r.u8();
      if (r.bad || seq != mine_.seq) return;
      friendVerdictSeq_ = seq;
      friendVerdict_.ok = ok != 0;
      friendVerdict_.text = clip(r.rest());
      break;
    }
    case kReady: {
      uint32_t own = r.u32(), other = r.u32(), flag = r.u8();
      if (r.bad) return;
      if (state_ == State::Committing && !peerAck_ && flag == 0) {
        end(Outcome::NothingChanged, "Your friend took back their Ready. Nothing was changed.");
        return;
      }
      if (state_ == State::Committing) return;
      peerReady_ = flag ? Pair{own, other} : Pair{};
      peerYes_ = false;
      break;
    }
    case kConfirm: {
      uint32_t own = r.u32(), other = r.u32();
      uint64_t dg = r.u64();
      uint32_t yes = r.u8();
      if (r.bad || !yes) return;
      peerYesPair_ = {own, other};
      peerYesDigest_ = dg;
      peerYes_ = true;
      break;
    }
    case kAck: {
      uint64_t dg = r.u64();
      if (r.bad) return;
      if (state_ != State::Committing && !(state_ == State::Confirming)) return;
      if (dg != digest()) {
        end(Outcome::NothingChanged, "The two games disagree about what is being traded. Nothing was changed.");
        return;
      }
      peerAck_ = true;
      break;
    }
    case kCancel: {
      std::string t = clip(r.rest());
      if (state_ == State::Committing && peerAck_) return;
      end(Outcome::NothingChanged, t.empty() ? "Your friend left the trade. Nothing was changed."
                                             : "Your friend's game can't finish the trade: " + t + " Nothing was changed.");
      return;
    }
    default:
      return;
  }
  lastProgressMs_ = nowMs_;
  advance();
}

void TradeSession::onDisconnected() {
  if (state_ == State::Done || state_ == State::Ended) return;
  if (state_ == State::Committing) {
    end(Outcome::Unknown,
        "The connection dropped while finishing. Nothing was saved on this PC. If your friend's game did save, "
        "the trade can be redone; check your Pokemon.");
  } else {
    end(Outcome::NothingChanged, "The connection dropped. Nothing was changed.");
  }
}

void TradeSession::tick(uint64_t now) {
  nowMs_ = now;
  if (lastProgressMs_ == 0) lastProgressMs_ = now;
  if (state_ == State::Committing && now - lastProgressMs_ > kCommitTimeoutMs) onDisconnected();
}

std::string TradeSession::status() const {
  switch (state_) {
    case State::Negotiating:
      if (mine_.record.empty()) return "Choose one of your Pokemon to offer.";
      if (theirs_.record.empty()) return "Waiting for your friend to offer a Pokemon.";
      if (!theirsVerdict_.ok) return "Can't take it: " + theirsVerdict_.text;
      if (!myOfferJudged()) return "Waiting for your friend's game to check your Pokemon.";
      if (!friendVerdict_.ok) return "Your friend's game can't take it: " + friendVerdict_.text;
      if (iAmReady()) return "Waiting for your friend to be Ready.";
      if (friendIsReady()) return "Your friend is Ready.";
      return "Press Ready when you are happy with the trade.";
    case State::Confirming:
      if (myYes_) return "Waiting for your friend to answer.";
      return "Both Ready. Answer Yes or No.";
    case State::Committing: return "Finishing the trade...";
    case State::Done:
    case State::Ended: return endText_;
  }
  return "";
}

}
