

#include "../src/online_draft.h"

#include <cstdio>
#include <deque>
#include <set>
#include <string>

using namespace xd::online;
using namespace xd::online::draft;

static int sFailed = 0, sChecks = 0;
#define CHECK(c)                                                         \
  do {                                                                   \
    sChecks++;                                                           \
    if (!(c)) {                                                          \
      sFailed++;                                                         \
      std::printf("FAILED line %d: %s\n", __LINE__, #c);                 \
    }                                                                    \
  } while (0)


static std::vector<Species> MakeTable() {
  std::vector<Species> t;
  for (int id = 1; id <= 90; id++) {
    Species s;
    s.id = static_cast<uint16_t>(id);
    s.valid = id % 17 != 0;
    s.gen = id <= 30 ? 1 : id <= 60 ? 2 : id <= 78 ? 3 : 4;
    if (id <= 60) {
      const int pos = (id - 1) % 3;
      s.fullyEvolved = pos == 2;
      s.hasPreEvo = pos > 0;
    } else {
      s.fullyEvolved = true;
      s.hasPreEvo = false;
    }
    s.legend = id == 70 || id == 71 || id == 85 || id == 86;
    t.push_back(s);
  }
  return t;
}


struct Pipe {
  std::deque<std::string> toA, toB;
  std::function<std::string(const std::string&)> tamper;
  bool closed = false;
};
struct End : BattleTransport {
  Pipe* p;
  bool isA;
  End(Pipe* pp, bool a) : p(pp), isA(a) {}
  bool Send(const uint8_t* d, size_t n) override {
    if (p->closed) return false;
    std::string m(reinterpret_cast<const char*>(d), n);
    if (isA) p->toB.push_back(p->tamper ? p->tamper(m) : m); else p->toA.push_back(m);
    return true;
  }
  int Receive(uint8_t* b, size_t cap) override {
    std::deque<std::string>& q = isA ? p->toA : p->toB;
    if (q.empty()) return p->closed ? -1 : 0;
    std::string m = q.front();
    q.pop_front();
    if (m.size() > cap) m.resize(cap);
    for (size_t i = 0; i < m.size(); i++) b[i] = static_cast<uint8_t>(m[i]);
    return static_cast<int>(m.size());
  }
  bool Connected() const override { return !p->closed; }
};

static double sClock = 0;
static int sNonce = 0;
static std::string Nonce() {
  char b[40];
  std::snprintf(b, sizeof b, "%032x", 0x1000 + (++sNonce));
  return b;
}

struct Pair {
  Pipe pipe;
  End a{&pipe, true}, b{&pipe, false};
  Session host, guest;
  Pair(const Settings& s, int timer, const std::vector<Species>& tHost, const std::vector<Species>& tGuest)
      : host(true, s, timer, tHost, &a, Nonce, [] { return sClock; }), guest(false, s, timer, tGuest, &b, Nonce, [] { return sClock; }) {}
  void Pump() { host.Poll(); guest.Poll(); host.Poll(); guest.Poll(); }
};

static bool Over(Session& s) { return s.state() == Session::State::Done || s.state() == Session::State::Failed; }


static void Play(Pair& pr, int rounds = 400) {
  const Settings& s = pr.host.Ready() ? pr.host.draft().settings() : Settings();
  (void)s;
  for (int i = 0; i < rounds && !(Over(pr.host) && Over(pr.guest)); i++) {
    pr.Pump();
    for (Session* me : {&pr.host, &pr.guest}) {
      if (!me->Ready()) continue;
      if (me->state() == Session::State::Banning) {
        std::vector<uint16_t> bans;
        const int n = me->draft().settings().bans;
        const std::vector<uint16_t>& pool = me->draft().pool();
        for (int k = 0; k < n; k++) bans.push_back(me->me() == 0 ? pool[static_cast<size_t>(k)] : pool[pool.size() - 1 - static_cast<size_t>(k)]);
        me->ChooseBans(bans);
      } else if (me->MyTurn()) {
        uint16_t id = 0;
        if (me->draft().settings().mode == "snake") {
          for (uint16_t e : me->draft().pool()) {
            if (me->draft().MarkOf(e) == Mark::Free) { id = e; break; }
          }
        } else {
          id = me->draft().CurrentOffer(me->me())[0];
        }
        me->Choose(id);
      }
    }
  }
  pr.Pump();
}

static bool Distinct(const std::vector<uint16_t>& v) { return std::set<uint16_t>(v.begin(), v.end()).size() == v.size(); }

int main() {
  const std::vector<Species> table = MakeTable();
  std::string err;
  Settings s;


  CHECK(ParseSettings("draft", "", s, &err) && s.teamSize == 6 && s.poolSize == 24 && s.bans == 0 && !s.gen4 && s.mode == "snake");
  CHECK(ParseSettings("draft", "size:3,pool:12,bans:1,first:coin,gen:4,evo:final,legend:no", s, &err));
  CHECK(s.teamSize == 3 && s.poolSize == 12 && s.bans == 1 && s.first == "coin" && s.gen4 && s.fullyEvolved && s.finalStage && s.noLegend);
  Settings back;
  CHECK(ParseSettings("draft", SettingsText(s), back, &err) && SettingsText(back) == SettingsText(s));
  CHECK(!ParseSettings("draft", "size:7", s, &err));
  CHECK(!ParseSettings("draft", "pool:11,size:6", s, &err));
  CHECK(!ParseSettings("draft", "pool:12,size:4,bans:3", s, &err));
  CHECK(!ParseSettings("draft", "colour:red", s, &err) && err.find("colour") != std::string::npos);
  CHECK(!ParseSettings("offer", "bans:1", s, &err));
  CHECK(ParseSettings("offer", "size:6,offer:3", s, &err) && s.mode == "offer");
  CHECK(!SettingsDescribe(s).empty());


  Settings f;
  ParseSettings("draft", "pool:20,size:2", f, &err);
  const std::vector<uint16_t> all3 = Candidates(f, table);
  for (uint16_t id : all3) CHECK(table[id - 1].valid && table[id - 1].gen <= 3);
  CHECK(all3.size() == 78 - 4);
  ParseSettings("draft", "pool:20,size:2,gen:4", f, &err);
  CHECK(Candidates(f, table).size() == 90 - 5);
  ParseSettings("draft", "pool:20,size:2,evo:full", f, &err);
  for (uint16_t id : Candidates(f, table)) CHECK(table[id - 1].fullyEvolved);
  ParseSettings("draft", "pool:20,size:2,evo:final", f, &err);
  for (uint16_t id : Candidates(f, table)) CHECK(table[id - 1].fullyEvolved && table[id - 1].hasPreEvo);
  ParseSettings("draft", "pool:20,size:2,evo:full,gen:4", f, &err);
  {
    size_t singles = 0;
    for (uint16_t id : Candidates(f, table)) singles += id > 60;
    CHECK(singles > 0);
    ParseSettings("draft", "pool:20,size:2,evo:final,gen:4", f, &err);
    for (uint16_t id : Candidates(f, table)) CHECK(id <= 60);
  }
  ParseSettings("draft", "pool:20,size:2,legend:no,gen:4", f, &err);
  for (uint16_t id : Candidates(f, table)) CHECK(!table[id - 1].legend);
  CHECK(!CheckForRoom(f, "classic", table, &err) && err.find("Gen 4") != std::string::npos);
  CHECK(CheckForRoom(f, "gen4", table, &err));
  ParseSettings("draft", "pool:100,size:2,evo:final", f, &err);
  CHECK(!CheckForRoom(f, "classic", table, &err) && err.find("filters") != std::string::npos);


  ParseSettings("draft", "pool:24,size:6", s, &err);
  const std::vector<uint16_t> p1 = MakePool(s, table, 1234), p2 = MakePool(s, table, 1234), p3 = MakePool(s, table, 1235);
  CHECK(p1.size() == 24 && p1 == p2 && p1 != p3 && Distinct(p1));
  for (uint16_t id : p1) CHECK(table[id - 1].valid && table[id - 1].gen <= 3);


  const int want[12] = {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0};
  for (int i = 0; i < 12; i++) CHECK(SnakeTurn(i, 0) == want[i] && SnakeTurn(i, 1) == 1 - want[i]);
  {
    Settings c;
    ParseSettings("draft", "first:coin", c, &err);
    int ones = 0;
    for (uint64_t sd = 1; sd <= 200; sd++) ones += FirstPlayer(c, sd);
    CHECK(ones > 60 && ones < 140);
    CHECK(FirstPlayer(s, 99) == 0);
  }


  {
    Settings d;
    ParseSettings("draft", "size:2,pool:12,bans:1", d, &err);
    const std::vector<uint16_t> pool = MakePool(d, table, 7);
    Draft dr(d, table, pool, 7);
    std::string why;
    CHECK(dr.phase() == Draft::Phase::Banning && dr.Turn() == -1);
    CHECK(!dr.Pick(0, pool[0], &why));
    CHECK(!dr.SubmitBans(0, {pool[0], pool[1]}, &why));
    CHECK(!dr.SubmitBans(0, {9999}, &why));
    CHECK(dr.SubmitBans(0, {pool[0]}, &why));
    CHECK(!dr.SubmitBans(0, {pool[1]}, &why));
    CHECK(dr.SubmitBans(1, {pool[0]}, &why));
    CHECK(dr.phase() == Draft::Phase::Picking && dr.MarkOf(pool[0]) == Mark::Banned);
    CHECK(dr.Turn() == 0);
    CHECK(!dr.Pick(1, pool[1], &why) && why == "not your turn");
    CHECK(!dr.Pick(0, pool[0], &why) && why == "already taken");
    CHECK(!dr.Pick(0, 9999, &why) && why == "not in the pool");
    CHECK(dr.Pick(0, pool[1], &why) && dr.MarkOf(pool[1]) == Mark::PickedByHost);
    CHECK(dr.Turn() == 1);
    CHECK(!dr.Pick(1, pool[1], &why) && why == "already taken");
    CHECK(dr.Pick(1, pool[2], &why));
    CHECK(dr.Turn() == 1 && dr.Pick(1, pool[3], &why));
    CHECK(dr.Turn() == 0 && dr.AutoPick(0) != 0 && dr.MarkOf(dr.AutoPick(0)) == Mark::Free);
    CHECK(dr.Pick(0, dr.AutoPick(0), &why));
    CHECK(dr.Done() && dr.Team(0).size() == 2 && dr.Team(1).size() == 2);
    CHECK(!dr.Pick(0, pool[5], &why));

    Draft d2(d, table, pool, 7);
    CHECK(d2.AutoBans(0).size() == 1 && d2.AutoBans(0) == d2.AutoBans(0));
  }


  {
    Settings o;
    ParseSettings("offer", "size:3,offer:3", o, &err);
    const std::vector<uint16_t> a = Offer(o, table, 5, 0, 0, {}), b = Offer(o, table, 5, 1, 0, {}), a1 = Offer(o, table, 5, 0, 1, {});
    CHECK(a.size() == 3 && Distinct(a) && a == Offer(o, table, 5, 0, 0, {}));
    CHECK(a != b && a != a1);
    const std::vector<uint16_t> ex = Offer(o, table, 5, 0, 0, {a[0], a[1], a[2]});
    for (uint16_t id : ex) CHECK(id != a[0] && id != a[1] && id != a[2]);
    Draft dr(o, table, {}, 5);
    std::string why;
    CHECK(dr.CanPick(0) && dr.CanPick(1) && dr.Turn() == -1);
    CHECK(!dr.Pick(0, 9999, &why));
    CHECK(dr.Pick(0, dr.CurrentOffer(0)[1], &why) && dr.Pick(0, dr.CurrentOffer(0)[2], &why) && dr.Pick(0, dr.CurrentOffer(0)[0], &why));
    CHECK(!dr.CanPick(0) && dr.CurrentOffer(0).empty() && !dr.Done());
    for (int i = 0; i < 3; i++) CHECK(dr.Pick(1, dr.AutoPick(1), &why));
    CHECK(dr.Done() && Distinct(dr.Team(0)) && Distinct(dr.Team(1)));
  }


  for (const char* variant : {"size:3,pool:16", "size:3,pool:16,bans:2,first:coin", "size:6,pool:30,bans:3,evo:full", "size:2,pool:8,gen:4,legend:no"}) {
    Settings d;
    CHECK(ParseSettings("draft", variant, d, &err));
    Pair pr(d, 0, table, table);
    pr.host.Start(0xABCDEF12345ull);
    Play(pr);
    CHECK(pr.host.state() == Session::State::Done && pr.guest.state() == Session::State::Done);
    CHECK(pr.host.Team(0) == pr.guest.Team(0) && pr.host.Team(1) == pr.guest.Team(1));
    CHECK(pr.host.Team(0).size() == static_cast<size_t>(d.teamSize) && pr.host.Team(1).size() == static_cast<size_t>(d.teamSize));
    std::set<uint16_t> seen;
    for (int p = 0; p < 2; p++) for (uint16_t id : pr.host.Team(p)) CHECK(seen.insert(id).second);
  }
  {
    Settings d;
    ParseSettings("offer", "size:4,offer:3", d, &err);
    Pair pr(d, 0, table, table);
    pr.host.Start(42);
    Play(pr);
    CHECK(pr.host.state() == Session::State::Done && pr.guest.state() == Session::State::Done);
    CHECK(pr.host.Team(0) == pr.guest.Team(0) && pr.host.Team(1) == pr.guest.Team(1) && pr.host.Team(0).size() == 4);
  }

  {
    Settings d;
    ParseSettings("draft", "size:3,pool:16,bans:2", d, &err);
    Pair pr(d, 30, table, table);
    pr.host.Start(77);
    sClock = 100;
    for (int i = 0; i < 200 && !(Over(pr.host) && Over(pr.guest)); i++) {
      sClock += 31;
      pr.Pump();
    }
    CHECK(pr.host.state() == Session::State::Done && pr.guest.state() == Session::State::Done);
    CHECK(pr.host.Team(0) == pr.guest.Team(0) && pr.host.Team(1) == pr.guest.Team(1) && pr.host.Team(0).size() == 3);

    Pair q(d, 30, table, table);
    q.host.Start(78);
    q.Pump();
    sClock = 1000;
    q.Pump();
    CHECK(q.host.TimeLeft() >= 0);
  }

  {
    Settings d;
    ParseSettings("draft", "size:3,pool:16", d, &err);
    std::vector<Species> other = table;
    for (Species& sp : other) if (sp.id % 2) sp.legend = !sp.legend, sp.fullyEvolved = !sp.fullyEvolved;
    other[4].valid = false;
    other[5].gen = 4;
    Settings e;
    ParseSettings("draft", "size:3,pool:16,evo:full,legend:no", e, &err);
    Pair pr(e, 0, table, other);
    pr.host.Start(9);
    pr.Pump();
    CHECK(pr.guest.state() == Session::State::Failed && pr.guest.error().find("does not match") != std::string::npos);
    pr.Pump();
    CHECK(pr.host.state() == Session::State::Failed);
  }

  {
    Settings d;
    ParseSettings("draft", "size:2,pool:12,bans:1", d, &err);
    Pair pr(d, 0, table, table);

    pr.pipe.tamper = [](const std::string& m) {
      if (m.compare(0, 5, "P BV ") == 0) return m.substr(0, m.find("B:")) + "B:1";
      return m;
    };
    pr.host.Start(31);
    Play(pr);
    CHECK(pr.guest.state() == Session::State::Failed && pr.guest.error().find("bans do not match") != std::string::npos);
    CHECK(pr.host.state() == Session::State::Failed);
  }

  {
    Settings d;
    ParseSettings("draft", "size:2,pool:8", d, &err);
    Pair pr(d, 0, table, table);
    pr.host.Start(5);
    pr.Pump();

    const std::string line = "P K 0 " + std::to_string(pr.guest.draft().pool()[0]);
    pr.b.Send(reinterpret_cast<const uint8_t*>(line.data()), line.size());
    pr.host.Poll();
    CHECK(pr.host.state() == Session::State::Picking);
    CHECK(!pr.pipe.toB.empty() && pr.pipe.toB.back().compare(0, 4, "P N ") == 0);
    CHECK(pr.host.draft().Team(1).empty() && pr.host.MyTurn());
    pr.b.Send(reinterpret_cast<const uint8_t*>(line.data()), line.size());
    pr.host.Poll();
    CHECK(pr.host.state() == Session::State::Failed && pr.host.error().find("not allowed") != std::string::npos);
  }

  {
    Settings d;
    ParseSettings("draft", "size:2,pool:8", d, &err);
    Pair pr(d, 0, table, table);
    int tampered = 0;
    pr.pipe.tamper = [&](const std::string& m) {
      if (m.compare(0, 4, "P K ") == 0 && tampered++ == 0) return m.substr(0, 6) + "9999";
      return m;
    };
    pr.host.Start(5);
    Play(pr);
    CHECK(tampered >= 2);
    CHECK(pr.host.state() == Session::State::Done && pr.guest.state() == Session::State::Done);
    CHECK(pr.host.Team(0) == pr.guest.Team(0) && pr.host.Team(1) == pr.guest.Team(1) && pr.host.Team(0).size() == 2);
    CHECK(pr.host.lastRefusal.find("refused") != std::string::npos);
  }

  {
    Settings d;
    ParseSettings("draft", "size:2,pool:8", d, &err);
    Pair pr(d, 0, table, table);
    pr.pipe.tamper = [](const std::string& m) { return m.compare(0, 4, "P K ") == 0 ? m.substr(0, 6) + "9999" : m; };
    pr.host.Start(5);
    Play(pr);
    CHECK(pr.host.state() == Session::State::Failed && pr.guest.state() == Session::State::Failed);
  }

  {
    Settings d;
    ParseSettings("draft", "size:1,pool:8", d, &err);
    Pair pr(d, 0, table, table);
    pr.host.Start(5);
    pr.Pump();
    pr.host.Choose(pr.host.draft().pool()[0]);
    pr.Pump();
    pr.guest.Choose(pr.guest.draft().pool()[1]);
    pr.Pump();
    CHECK(pr.host.state() == Session::State::Done);
  }

  {
    Settings d;
    ParseSettings("draft", "size:2,pool:8", d, &err);
    Pair pr(d, 0, table, table);
    pr.host.Start(5);
    pr.Pump();
    pr.pipe.closed = true;
    pr.Pump();
    CHECK(pr.host.state() == Session::State::Failed && pr.host.error().find("gone") != std::string::npos);
  }

  std::printf("online_draft_test: %d checks, %d failed\n", sChecks, sFailed);
  return sFailed == 0 ? 0 : 1;
}
