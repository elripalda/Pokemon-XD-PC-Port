#include "online_draft.h"

#include "online_commit.h"
#include "i18n.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace xd::online::draft {

namespace {

using engine::i18n::Tr;

template <class... A> std::string Fmt(const char* f, A... a) {
  char b[512];
  std::snprintf(b, sizeof b, f, a...);
  return b;
}

struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}
  uint64_t next() {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  size_t below(size_t n) { return static_cast<size_t>(next() % n); }
};

void Shuffle(std::vector<uint16_t>& v, Rng& r) {
  for (size_t i = v.size(); i > 1; i--) std::swap(v[i - 1], v[r.below(i)]);
}

std::vector<std::string> Split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) { out.push_back(cur); cur.clear(); } else cur += c;
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

bool Number(const std::string& v, int lo, int hi, int& out) {
  if (v.empty() || v.size() > 5 || v.find_first_not_of("0123456789") != std::string::npos) return false;
  const int n = std::atoi(v.c_str());
  if (n < lo || n > hi) return false;
  out = n;
  return true;
}

bool Has(const std::vector<uint16_t>& v, uint16_t id) { return std::find(v.begin(), v.end(), id) != v.end(); }

std::string IdList(const std::vector<uint16_t>& ids) {
  std::string out;
  for (uint16_t id : ids) out += (out.empty() ? "" : ",") + std::to_string(id);
  return out;
}

bool ParseIds(const std::string& text, std::vector<uint16_t>& out) {
  out.clear();
  for (const std::string& t : Split(text, ',')) {
    int n = 0;
    if (!Number(t, 1, 65535, n)) return false;
    out.push_back(static_cast<uint16_t>(n));
  }
  return true;
}

uint64_t Fnv(uint64_t h, uint16_t v) {
  for (int i = 0; i < 2; i++) { h ^= (v >> (8 * i)) & 0xFF; h *= 1099511628211ull; }
  return h;
}

}

bool ParseSettings(const std::string& mode, const std::string& text, Settings& out, std::string* error) {
  auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
  Settings s;
  if (mode != "snake" && mode != "offer" && mode != "draft") return fail(Tr("the team source is not a draft"));
  s.mode = mode == "offer" ? "offer" : "snake";
  for (const std::string& item : Split(text, ',')) {
    const size_t c = item.find(':');
    if (c == std::string::npos) return fail(Fmt(Tr("'%s' is not key:value"), item.c_str()));
    const std::string k = item.substr(0, c), v = item.substr(c + 1);
    int n = 0;
    if (k == "size") { if (!Number(v, 1, 6, n)) return fail(Tr("size must be 1 to 6")); s.teamSize = n; }
    else if (k == "pool") { if (!Number(v, 4, 120, n)) return fail(Tr("pool must be 4 to 120")); s.poolSize = n; }
    else if (k == "bans") { if (!Number(v, 0, 5, n)) return fail(Tr("bans must be 0 to 5")); s.bans = n; }
    else if (k == "offer") { if (!Number(v, 2, 5, n)) return fail(Tr("offer must be 2 to 5")); s.offerCount = n; }
    else if (k == "first") { if (v != "host" && v != "coin") return fail(Tr("first must be host or coin")); s.first = v; }
    else if (k == "gen") { if (v != "3" && v != "4") return fail(Tr("gen must be 3 (Gen 1-3) or 4 (Gen 1-4)")); s.gen4 = v == "4"; }
    else if (k == "evo") {
      if (v != "any" && v != "full" && v != "final") return fail(Tr("evo must be any, full or final"));
      s.fullyEvolved = v != "any";
      s.finalStage = v == "final";
    }
    else if (k == "legend") { if (v != "yes" && v != "no") return fail(Tr("legend must be yes or no")); s.noLegend = v == "no"; }
    else return fail(Fmt(Tr("unknown draft option '%s'"), k.c_str()));
  }
  if (s.mode == "snake" && s.poolSize - 2 * s.bans < 2 * s.teamSize) return fail(Tr("the pool is too small for that team size and number of bans"));
  if (s.mode == "offer" && s.bans != 0) return fail(Tr("bans are only for the snake draft"));
  out = s;
  return true;
}

std::string SettingsText(const Settings& s) {
  return "size:" + std::to_string(s.teamSize) + ",pool:" + std::to_string(s.poolSize) + ",bans:" + std::to_string(s.bans) + ",first:" + s.first +
         ",gen:" + (s.gen4 ? "4" : "3") + ",evo:" + (s.finalStage ? "final" : s.fullyEvolved ? "full" : "any") + ",legend:" + (s.noLegend ? "no" : "yes") +
         ",offer:" + std::to_string(s.offerCount);
}

std::string SettingsDescribe(const Settings& s) {
  std::string o = s.mode == "offer" ? Fmt(Tr("Random offers: %d to choose from for each of %d places"), s.offerCount, s.teamSize)
                                    : Fmt(Tr("Snake draft: %d Pokemon in the pool, %d each"), s.poolSize, s.teamSize);
  if (s.bans > 0) o += ", " + (s.bans > 1 ? Fmt(Tr("%d bans each first"), s.bans) : Fmt(Tr("%d ban each first"), s.bans));
  o += std::string("; ") + (s.gen4 ? Tr("Gen 1-4") : Tr("Gen 1-3"));
  if (s.finalStage) o += std::string("; ") + Tr("final stages only");
  else if (s.fullyEvolved) o += std::string("; ") + Tr("fully evolved only");
  if (s.noLegend) o += std::string("; ") + Tr("no legendaries");
  if (s.mode == "snake") o += std::string("; ") + (s.first == "coin" ? Tr("first pick by coin flip") : Tr("the host picks first"));
  return o;
}

std::vector<uint16_t> Candidates(const Settings& s, const std::vector<Species>& table) {
  std::vector<uint16_t> out;
  for (const Species& sp : table) {
    if (!sp.valid || sp.id == 0) continue;
    if (sp.gen > (s.gen4 ? 4 : 3)) continue;
    if (s.fullyEvolved && !sp.fullyEvolved) continue;
    if (s.finalStage && !sp.hasPreEvo) continue;
    if (s.noLegend && sp.legend) continue;
    out.push_back(sp.id);
  }
  return out;
}

bool CheckForRoom(const Settings& s, const std::string& ruleSet, const std::vector<Species>& table, std::string* error) {
  auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
  if (s.gen4 && ruleSet == "classic") return fail(Tr("a Gen 1-4 draft needs the Gen 4 rules"));
  const size_t n = Candidates(s, table).size();
  if (s.mode == "snake" && n < static_cast<size_t>(s.poolSize)) return fail(Fmt(Tr("only %d Pokemon pass the filters; the pool wants %d"), static_cast<int>(n), s.poolSize));
  if (s.mode == "offer" && n < static_cast<size_t>(2 * s.teamSize + s.offerCount)) return fail(Tr("too few Pokemon pass the filters for the offers"));
  return true;
}

std::vector<uint16_t> MakePool(const Settings& s, const std::vector<Species>& table, uint64_t seed) {
  std::vector<uint16_t> c = Candidates(s, table);
  Rng r(seed ^ 0x504F4F4Cull);
  Shuffle(c, r);
  if (c.size() > static_cast<size_t>(s.poolSize)) c.resize(static_cast<size_t>(s.poolSize));
  return c;
}

int FirstPlayer(const Settings& s, uint64_t seed) {
  if (s.first != "coin") return 0;
  Rng r(seed ^ 0x434F494Eull);
  return static_cast<int>((r.next() >> 11) & 1);
}

int SnakeTurn(int i, int first) { return (((i + 1) / 2) % 2 == 0) ? first : 1 - first; }

std::vector<uint16_t> Offer(const Settings& s, const std::vector<Species>& table, uint64_t seed, int player, int slot, const std::vector<uint16_t>& taken) {
  std::vector<uint16_t> c;
  for (uint16_t id : Candidates(s, table)) {
    if (!Has(taken, id)) c.push_back(id);
  }
  Rng r(seed ^ (0x1000193ull * static_cast<uint64_t>(player + 1)) ^ (0x9E3779B1ull * static_cast<uint64_t>(slot + 1)) ^ 0x4F464552ull);
  Shuffle(c, r);
  if (c.size() > static_cast<size_t>(s.offerCount)) c.resize(static_cast<size_t>(s.offerCount));
  return c;
}

Draft::Draft(const Settings& s, const std::vector<Species>& table, std::vector<uint16_t> pool, uint64_t seed)
    : s_(s), table_(table), pool_(std::move(pool)), seed_(seed), first_(FirstPlayer(s, seed)),
      phase_(s.mode == "snake" && s.bans > 0 ? Phase::Banning : Phase::Picking) {}

bool Draft::InPool(uint16_t id) const { return Has(pool_, id); }

Mark Draft::MarkOf(uint16_t id) const {
  if (Has(banned_, id)) return Mark::Banned;
  if (Has(team_[0], id)) return Mark::PickedByHost;
  if (Has(team_[1], id)) return Mark::PickedByGuest;
  return Mark::Free;
}

int Draft::Turn() const {
  if (s_.mode != "snake" || phase_ != Phase::Picking) return -1;
  return SnakeTurn(picks_, first_);
}

bool Draft::CanPick(int p) const {
  if (phase_ != Phase::Picking || team_[p].size() >= static_cast<size_t>(s_.teamSize)) return false;
  return s_.mode == "offer" || Turn() == p;
}

std::vector<uint16_t> Draft::CurrentOffer(int p) const {
  if (s_.mode != "offer" || team_[p].size() >= static_cast<size_t>(s_.teamSize)) return {};
  return Offer(s_, table_, seed_, p, static_cast<int>(team_[p].size()), team_[p]);
}

bool Draft::SubmitBans(int p, const std::vector<uint16_t>& ids, std::string* why) {
  auto fail = [&](const char* m) { if (why) *why = m; return false; };
  if (phase_ != Phase::Banning) return fail("no bans now");
  if (bansIn_[p]) return fail("bans already given");
  if (ids.size() != static_cast<size_t>(s_.bans)) return fail("wrong number of bans");
  std::set<uint16_t> uniq(ids.begin(), ids.end());
  if (uniq.size() != ids.size()) return fail("the same Pokemon banned twice");
  for (uint16_t id : ids) {
    if (!InPool(id)) return fail("a banned Pokemon is not in the pool");
  }
  bansOf_[p] = ids;
  bansIn_[p] = true;
  if (bansIn_[0] && bansIn_[1]) {
    banned_ = bansOf_[0];
    for (uint16_t id : bansOf_[1]) {
      if (!Has(banned_, id)) banned_.push_back(id);
    }
    phase_ = Phase::Picking;
  }
  return true;
}

bool Draft::Pick(int p, uint16_t id, std::string* why) {
  auto fail = [&](const char* m) { if (why) *why = m; return false; };
  if (phase_ == Phase::Banning) return fail("the bans come first");
  if (phase_ == Phase::Done) return fail("the draft is over");
  if (!CanPick(p)) return fail("not your turn");
  if (s_.mode == "snake") {
    if (!InPool(id)) return fail("not in the pool");
    if (MarkOf(id) != Mark::Free) return fail("already taken");
  } else {
    if (!Has(CurrentOffer(p), id)) return fail("not one of the offered Pokemon");
  }
  team_[p].push_back(id);
  picks_++;
  if (team_[0].size() >= static_cast<size_t>(s_.teamSize) && team_[1].size() >= static_cast<size_t>(s_.teamSize)) phase_ = Phase::Done;
  return true;
}

void Draft::UndoLastPick(int p) {
  if (team_[p].empty()) return;
  team_[p].pop_back();
  picks_--;
  phase_ = Phase::Picking;
}

uint16_t Draft::AutoPick(int p) const {
  Rng r(seed_ ^ (static_cast<uint64_t>(picks_ + 1) * 0xA24BAED4963EE407ull) ^ static_cast<uint64_t>(p));
  if (s_.mode == "offer") {
    const std::vector<uint16_t> o = CurrentOffer(p);
    if (o.empty()) return 0;
    return o[r.below(o.size())];
  }
  std::vector<uint16_t> free;
  for (uint16_t id : pool_) {
    if (MarkOf(id) == Mark::Free) free.push_back(id);
  }
  if (free.empty()) return 0;
  return free[r.below(free.size())];
}

std::vector<uint16_t> Draft::AutoBans(int p) const {
  std::vector<uint16_t> c = pool_;
  Rng r(seed_ ^ (0xB5297A4D3F84D5B5ull * static_cast<uint64_t>(p + 1)));
  Shuffle(c, r);
  c.resize(std::min(c.size(), static_cast<size_t>(s_.bans)));
  return c;
}

uint64_t Draft::TeamsHash() const {
  uint64_t h = 1469598103934665603ull;
  for (uint16_t id : team_[0]) h = Fnv(h, id);
  h = Fnv(h, 0xFFFF);
  for (uint16_t id : team_[1]) h = Fnv(h, id);
  return h;
}

Session::Session(bool host, const Settings& s, int timerSeconds, const std::vector<Species>& table, BattleTransport* link, std::function<std::string()> nonce,
                 std::function<double()> now)
    : host_(host), s_(s), timer_(timerSeconds), table_(table), link_(link), nonce_(std::move(nonce)), now_(std::move(now)) {}

void Session::Send(const std::string& line) {
  if (!link_->Send(reinterpret_cast<const uint8_t*>(line.data()), line.size())) Fail(Tr("The connection to your friend is gone."), false);
}

void Session::Fail(const std::string& why, bool tell) {
  if (state_ == State::Failed || state_ == State::Done) return;
  if (tell) Send("P X " + why);
  state_ = State::Failed;
  error_ = why;
}

void Session::Start(uint64_t hostSeed) {
  if (!host_ || draft_) return;
  seed_ = hostSeed;
  std::vector<uint16_t> pool = s_.mode == "snake" ? MakePool(s_, table_, seed_) : std::vector<uint16_t>();
  if (s_.mode == "snake" && pool.size() < static_cast<size_t>(s_.poolSize)) { Fail(Tr("Not enough Pokemon pass the pool filters."), true); return; }
  draft_.reset(new Draft(s_, table_, pool, seed_));
  char hex[32];
  std::snprintf(hex, sizeof hex, "%llx", static_cast<unsigned long long>(seed_));
  Send(s_.mode == "snake" ? std::string("P POOL ") + hex + " " + IdList(pool) : std::string("P SEED ") + hex);
  state_ = draft_->phase() == Draft::Phase::Banning ? State::Banning : State::Picking;
  Advance();
}

bool Session::MyTurn() const { return state_ == State::Picking && draft_ && draft_->CanPick(me()); }

bool Session::Choose(uint16_t id, std::string* why) {
  if (state_ != State::Picking || !draft_) { if (why) *why = "not picking now"; return false; }
  const size_t slot = draft_->Team(me()).size();
  if (!draft_->Pick(me(), id, why)) return false;
  Send("P K " + std::to_string(slot) + " " + std::to_string(id));
  Advance();
  return true;
}

bool Session::ChooseBans(const std::vector<uint16_t>& ids, std::string* why) {
  if (state_ != State::Banning || !draft_ || sentBc_) { if (why) *why = "no bans now"; return false; }
  if (!draft_->SubmitBans(me(), ids, why)) return false;
  std::vector<uint16_t> sorted = ids;
  std::sort(sorted.begin(), sorted.end());
  myBanRecord_ = "B:" + IdList(sorted);
  myNonce_ = nonce_();
  myCommit_ = Commit(myNonce_, myBanRecord_);
  if (myCommit_.empty()) { Fail(Tr("The ban commitment could not be made."), true); return false; }
  Send("P BC " + myCommit_);
  sentBc_ = true;
  Advance();
  return true;
}

void Session::StartTimer() { deadline_ = timer_ > 0 ? now_() + timer_ : -1; }

double Session::TimeLeft() const {
  if (deadline_ < 0) return -1;
  return std::max(0.0, deadline_ - now_());
}

void Session::Advance() {
  if (state_ == State::Failed || state_ == State::Done || !draft_) return;
  if (state_ == State::Banning) {
    if (sentBc_ && !peerCommit_.empty() && !sentBv_) {
      Send("P BV " + myNonce_ + " " + myBanRecord_);
      sentBv_ = true;
    }
    if (sentBv_ && !peerBv_.empty()) {
      const size_t sp = peerBv_.find(' ');
      const std::string nonce = peerBv_.substr(0, sp), rec = sp == std::string::npos ? "" : peerBv_.substr(sp + 1);
      std::vector<uint16_t> ids;
      std::string why;
      if (!Verify(peerCommit_, nonce, rec) || rec.compare(0, 2, "B:") != 0 || !ParseIds(rec.substr(2), ids) ||
          !draft_->SubmitBans(1 - me(), ids, &why)) {
        Fail(Fmt(Tr("Your friend's bans do not match what they locked in (%s). The draft is stopped."), (why.empty() ? std::string(Tr("bad reveal")) : why).c_str()), true);
        return;
      }
      peerBv_.clear();
    }
    if (draft_->phase() == Draft::Phase::Picking) { state_ = State::Picking; timerFor_ = -1; }
    else if (timerFor_ != -2) { timerFor_ = -2; StartTimer(); }
  }
  if (state_ == State::Picking) {
    const int key = draft_->PickNumber() * 8 + static_cast<int>(draft_->Team(me()).size());
    if (timerFor_ != key) { timerFor_ = key; StartTimer(); }
    if (draft_->Done()) state_ = State::Finishing;
  }
  if (state_ == State::Finishing) {
    deadline_ = -1;
    if (!sentEnd_) {
      char hex[32];
      std::snprintf(hex, sizeof hex, "%llx", static_cast<unsigned long long>(draft_->TeamsHash()));
      Send(std::string("P E ") + hex);
      sentEnd_ = true;
    }
    if (gotEnd_) {
      if (peerEnd_ == draft_->TeamsHash()) state_ = State::Done;
      else Fail(Tr("The two games ended with different teams. The draft is stopped."), true);
    }
  }
}

void Session::Handle(const std::string& line) {
  if (line.compare(0, 2, "P ") != 0) { Fail(Tr("An unexpected message came during the draft."), true); return; }
  const size_t sp = line.find(' ', 2);
  const std::string type = line.substr(2, sp == std::string::npos ? std::string::npos : sp - 2);
  const std::string rest = sp == std::string::npos ? "" : line.substr(sp + 1);
  if (type == "X") { Fail(Fmt(Tr("Your friend stopped the draft: %s"), rest.c_str()), false); return; }
  if (type == "POOL" || type == "SEED") {
    if (host_ || draft_) { Fail(Tr("A damaged draft message."), true); return; }
    const size_t sp2 = rest.find(' ');
    seed_ = std::strtoull(rest.substr(0, sp2).c_str(), nullptr, 16);
    std::vector<uint16_t> pool;
    if (type == "POOL") {
      if (sp2 == std::string::npos || !ParseIds(rest.substr(sp2 + 1), pool)) { Fail(Tr("The pool message is damaged."), true); return; }
      if (pool != MakePool(s_, table_, seed_)) { Fail(Tr("The pool does not match this game's data (the two games differ). The draft is stopped."), true); return; }
    }
    draft_.reset(new Draft(s_, table_, pool, seed_));
    state_ = draft_->phase() == Draft::Phase::Banning ? State::Banning : State::Picking;
    Advance();
    return;
  }
  if (!draft_) { Fail(Tr("A draft message came before the pool."), true); return; }
  if (type == "BC") { if (!peerCommit_.empty() || rest.size() != 64) { Fail(Tr("A damaged ban message."), true); return; } peerCommit_ = rest; Advance(); return; }
  if (type == "BV") { if (peerCommit_.empty() || !peerBv_.empty()) { Fail(Tr("A ban reveal came out of order."), true); return; } peerBv_ = rest; Advance(); return; }
  if (type == "K") {
    int slot = -1, id = 0;
    if (std::sscanf(rest.c_str(), "%d %d", &slot, &id) != 2 || slot != static_cast<int>(draft_->Team(1 - me()).size()) || id < 1 || id > 65535) {
      Fail(Tr("A damaged pick message."), true);
      return;
    }
    std::string why;
    if (!draft_->Pick(1 - me(), static_cast<uint16_t>(id), &why)) {
      if (refusalsSent_++ > 0) { Fail(Fmt(Tr("Your friend's picks were not allowed (%s). The draft is stopped."), why.c_str()), true); return; }
      Send("P N " + std::to_string(slot) + " " + why);
      return;
    }
    Advance();
    return;
  }
  if (type == "N") {
    int slot = -1;
    char why[96] = "";
    std::sscanf(rest.c_str(), "%d %95[^\n]", &slot, why);
    if (refusalsGot_++ > 0 || slot < 0 || draft_->Team(me()).size() != static_cast<size_t>(slot) + 1) { Fail(Tr("Your friend refused your pick again. The draft is stopped."), true); return; }
    draft_->UndoLastPick(me());
    lastRefusal = Fmt(Tr("Your friend's game refused that pick (%s). Choose again."), why);
    sentEnd_ = false;
    if (state_ == State::Finishing) state_ = State::Picking;
    timerFor_ = -1;
    Advance();
    return;
  }
  if (type == "E") { if (!draft_->Done()) return;
    peerEnd_ = std::strtoull(rest.c_str(), nullptr, 16); gotEnd_ = true; Advance(); return; }
  Fail(Tr("An unknown draft message."), true);
}

void Session::Poll() {
  if (state_ == State::Failed || state_ == State::Done) return;
  for (int guard = 0; guard < 64 && state_ != State::Failed && state_ != State::Done; guard++) {
    uint8_t buf[4096];
    const int n = link_->Receive(buf, sizeof buf - 1);
    if (n < 0) { Fail(Tr("The connection to your friend is gone."), false); return; }
    if (n == 0) break;
    Handle(std::string(reinterpret_cast<char*>(buf), static_cast<size_t>(n)));
  }
  if (state_ == State::Failed || state_ == State::Done || !draft_) return;
  if (timer_ > 0 && deadline_ >= 0 && now_() >= deadline_) {
    if (state_ == State::Banning && !sentBc_) ChooseBans(draft_->AutoBans(me()));
    else if (state_ == State::Picking && draft_->CanPick(me())) {
      const uint16_t id = draft_->AutoPick(me());
      if (id) Choose(id);
    }
  }
}

}
