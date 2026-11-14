
#include "net_trade.h"

#include <cstdio>
#include <deque>
#include <string>

using namespace xdnet::trade;
using Bytes = std::vector<uint8_t>;

static int gChecks = 0, gFailed = 0;
#define CHECK(c) do { gChecks++; if (!(c)) { gFailed++; std::printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)

struct Side {
  TradeSession s;
  Bytes myRecord;
  int commits = 0, prepares = 0;
  bool refusePrepare = false;
  std::string refuseIncoming;
  Side(bool low, Bytes rec)
      : s(low, Hooks{}), myRecord(std::move(rec)) {}
};

static Hooks makeHooks(Side* me) {
  Hooks h;
  h.check = [me](const Bytes&) { return Verdict{me->refuseIncoming.empty(), me->refuseIncoming}; };
  h.prepare = [me](const Bytes&, const Bytes&) { me->prepares++; return Verdict{!me->refusePrepare, "no room"}; };
  h.commit = [me](const Bytes&, const Bytes&) { me->commits++; return Verdict{true, ""}; };
  return h;
}

struct Run {
  Side* a; Side* b;
  std::deque<Bytes> toA, toB;
  int delivered = 0;
  int cutAfter = -1;
  bool cut = false;
};

static void drive(Side& me) {
  TradeSession& s = me.s;
  if (s.state() == State::Done || s.state() == State::Ended) return;
  if (s.mine().record.empty()) { s.offer(me.myRecord); return; }
  if (s.state() == State::Negotiating && s.canReady() && !s.iAmReady()) { s.setReady(true); return; }
  if (s.state() == State::Confirming && !s.iConfirmed()) { s.confirm(true); return; }
}

static void pump(Run& r) {
  for (auto& m : r.a->s.takeOutgoing()) r.toB.push_back(m);
  for (auto& m : r.b->s.takeOutgoing()) r.toA.push_back(m);
}


static void runTrade(Run& r, Side& A, Side& B) {
  r.a = &A; r.b = &B;
  bool turnA = true;
  for (int step = 0; step < 2000; step++) {
    auto over = [](Side& x) { return x.s.state() == State::Done || x.s.state() == State::Ended; };
    if (over(A) && over(B)) break;
    drive(A); drive(B);
    pump(r);
    if (r.cutAfter >= 0 && r.delivered >= r.cutAfter && !r.cut) {
      r.cut = true;
      r.toA.clear(); r.toB.clear();
      A.s.onDisconnected(); B.s.onDisconnected();
      break;
    }
    bool toAnow = (turnA && !r.toA.empty()) || r.toB.empty();
    std::deque<Bytes>& q = toAnow ? r.toA : r.toB;
    Side& to = toAnow ? A : B;
    if (!q.empty()) { Bytes m = q.front(); q.pop_front(); r.delivered++; to.s.onMessage(m); }
    turnA = !turnA;
    if (r.toA.empty() && r.toB.empty()) {
      drive(A); drive(B); pump(r);
      if (r.toA.empty() && r.toB.empty() && !(over(A) && over(B))) break;
    }
  }
}

static Bytes rec(uint8_t v) { return Bytes(0xC4, v); }

int main() {

  {
    Side A(true, rec(1)), B(false, rec(2));
    A.s = TradeSession(true, makeHooks(&A));
    B.s = TradeSession(false, makeHooks(&B));
    Run r; runTrade(r, A, B);
    CHECK(A.s.state() == State::Done && B.s.state() == State::Done);
    CHECK(A.commits == 1 && B.commits == 1);
    CHECK(A.s.outcome() == Outcome::Traded);
    std::printf("clean trade: %d messages\n", r.delivered);
  }

  int total = 0;
  {
    Side A(true, rec(1)), B(false, rec(2));
    A.s = TradeSession(true, makeHooks(&A)); B.s = TradeSession(false, makeHooks(&B));
    Run r; runTrade(r, A, B); total = r.delivered;
  }
  int splits = 0, both = 0, neither = 0;
  for (int n = 0; n <= total; n++) {
    Side A(true, rec(1)), B(false, rec(2));
    A.s = TradeSession(true, makeHooks(&A)); B.s = TradeSession(false, makeHooks(&B));
    Run r; r.cutAfter = n; runTrade(r, A, B);
    const bool a = A.commits == 1, b = B.commits == 1;
    CHECK(A.commits <= 1 && B.commits <= 1);
    CHECK(A.s.state() == State::Done || A.s.state() == State::Ended);
    CHECK(B.s.state() == State::Done || B.s.state() == State::Ended);
    if (a && b) both++;
    else if (!a && !b) neither++;
    else {
      splits++;
      Side& loser = a ? B : A;
      CHECK(loser.s.outcome() == Outcome::Unknown);
    }
    if (!a && !b) CHECK(A.s.outcome() != Outcome::Traded && B.s.outcome() != Outcome::Traded);
    if (a && !b) CHECK(A.s.outcome() == Outcome::Traded);
  }
  std::printf("drops at %d points: both saved %d, neither %d, the two-generals window %d\n", total + 1, both, neither, splits);
  CHECK(neither >= total - 2);
  CHECK(splits <= 1);

  {

    Side A(true, rec(1)), B(false, rec(2));
    A.s = TradeSession(true, makeHooks(&A)); B.s = TradeSession(false, makeHooks(&B));
    Run r; runTrade(r, A, B);
  }

  {
    Side A(true, rec(1)), B(false, rec(2));
    B.refuseIncoming = "Gen 4 Pokemon need the Gen 4 option.";
    A.s = TradeSession(true, makeHooks(&A)); B.s = TradeSession(false, makeHooks(&B));
    Run r; runTrade(r, A, B);
    CHECK(A.commits == 0 && B.commits == 0);
    CHECK(A.s.myOfferJudged() && !A.s.myOfferVerdict().ok);
    CHECK(A.s.myOfferVerdict().text == "Gen 4 Pokemon need the Gen 4 option.");
    CHECK(!A.s.canReady());
    CHECK(!A.s.setReady(true) && !A.s.why().empty());
  }

  {
    Side A(true, rec(1)), B(false, rec(2));
    B.refusePrepare = true;
    A.s = TradeSession(true, makeHooks(&A)); B.s = TradeSession(false, makeHooks(&B));
    Run r; runTrade(r, A, B);
    CHECK(A.commits == 0 && B.commits == 0);
    CHECK(A.s.outcome() == Outcome::NothingChanged && B.s.outcome() == Outcome::NothingChanged);
  }

  {
    Side A(true, rec(1)), B(false, rec(2));
    A.s = TradeSession(true, makeHooks(&A)); B.s = TradeSession(false, makeHooks(&B));
    Run r; r.a = &A; r.b = &B;
    auto flush = [&]() { for (int i = 0; i < 20; i++) { pump(r);
      while (!r.toA.empty()) { Bytes m = r.toA.front(); r.toA.pop_front(); A.s.onMessage(m); }
      while (!r.toB.empty()) { Bytes m = r.toB.front(); r.toB.pop_front(); B.s.onMessage(m); } } };
    A.s.offer(rec(1)); B.s.offer(rec(2)); flush();
    CHECK(A.s.canReady() && B.s.canReady());
    A.s.setReady(true); flush();
    CHECK(A.s.iAmReady() && B.s.friendIsReady() && !B.s.iAmReady());
    B.s.offer(rec(3)); flush();
    CHECK(!B.s.friendIsReady() && !A.s.iAmReady());
    A.s.setReady(true); B.s.setReady(true); flush();
    CHECK(A.s.state() == State::Confirming && B.s.state() == State::Confirming);
    A.s.confirm(true); flush();
    CHECK(A.s.iConfirmed() && B.s.friendConfirmed() && A.commits == 0);
    B.s.confirm(false); flush();
    CHECK(A.s.state() == State::Negotiating && B.s.state() == State::Negotiating);
    CHECK(!A.s.iConfirmed());
    A.s.withdraw(); flush();
    CHECK(B.s.theirs().record.empty() && !B.s.canReady());
    A.s.cancel(); flush();
    CHECK(A.s.outcome() == Outcome::NothingChanged && B.s.outcome() == Outcome::NothingChanged);
    CHECK(A.commits == 0 && B.commits == 0);
  }

  {
    Side A(true, rec(1));
    A.s = TradeSession(true, makeHooks(&A));
    A.s.onMessage({});
    A.s.onMessage({99, 1, 2, 3});
    A.s.onMessage({1, 0, 0});
    A.s.onMessage({1, 1, 0, 0, 0, 0xFF, 0xFF});
    A.s.onMessage({5, 1});
    CHECK(A.s.state() == State::Negotiating && A.s.theirs().seq == 0);
  }
  std::printf("net_trade_test: %d checks, %d failed\n", gChecks, gFailed);
  return gFailed ? 1 : 0;
}
