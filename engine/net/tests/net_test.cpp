


#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <vector>

#include "net_connection.h"
#include "net_crypto.h"
#include "net_proto.h"
#include "net_session.h"

using namespace xdnet;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                  \
  do {                                                                            \
    g_checks++;                                                                   \
    if (!(c)) {                                                                   \
      g_fail++;                                                                   \
      std::printf("FAILED line %d: %s\n", __LINE__, #c);                          \
    }                                                                             \
  } while (0)


struct Sim {
  struct Pkt {
    int to;
    std::vector<uint8_t> d;
  };
  std::multimap<uint64_t, Pkt> q;
  std::mt19937 rng{12345};
  uint64_t now = 1000;
  double loss = 0, dup = 0;
  uint32_t baseMs = 20, jitterMs = 0;
  bool blackhole = false;
  std::function<void(int from, std::vector<uint8_t>&)> tamper;
  std::vector<std::vector<uint8_t>> seenData;
  Endpoint ep[2] = {Endpoint::v4(10, 0, 0, 1, 1000), Endpoint::v4(10, 0, 0, 2, 2000)};
  std::unique_ptr<Session> s[2];
  uint64_t sent = 0;

  double rnd() { return std::uniform_real_distribution<double>(0, 1)(rng); }
  void make(const std::string& codeA, const std::string& codeB, SessionConfig ca = {}, SessionConfig cb = {}) {
    s[0].reset(new Session(codeA, ca, [this](const Endpoint& to, const uint8_t* d, size_t n) { if (to != ep[1]) { sent++; return; } push(0, d, n); }));
    s[1].reset(new Session(codeB, cb, [this](const Endpoint& to, const uint8_t* d, size_t n) { if (to != ep[0]) { sent++; return; } push(1, d, n); }));
    s[0]->addCandidate(ep[1]);
    s[1]->addCandidate(ep[0]);
  }
  void push(int from, const uint8_t* d, size_t n) {
    sent++;
    if (blackhole) return;
    std::vector<uint8_t> v(d, d + n);
    if (d[0] == 0xA3) seenData.push_back(v);
    if (tamper) tamper(from, v);
    int copies = rnd() < dup ? 2 : 1;
    for (int i = 0; i < copies; i++) {
      if (rnd() < loss) continue;
      uint64_t at = now + baseMs + (jitterMs ? rng() % (jitterMs + 1) : 0);
      q.insert({at, Pkt{1 - from, v}});
    }
  }
  void step(uint32_t ms = 5) {
    now += ms;
    while (!q.empty() && q.begin()->first <= now) {
      Pkt p = std::move(q.begin()->second);
      q.erase(q.begin());
      s[p.to]->onDatagram(ep[1 - p.to], p.d.data(), p.d.size(), now);
    }
    s[0]->update(now);
    s[1]->update(now);
  }
  bool run(uint64_t maxMs, const std::function<bool()>& done) {
    for (uint64_t t = 0; t < maxMs; t += 5) {
      step();
      if (done()) return true;
    }
    return false;
  }
  bool connect(uint64_t maxMs = 20000) {
    return run(maxMs, [&] { return s[0]->state() == SessionState::Connected && s[1]->state() == SessionState::Connected; });
  }
};

static std::vector<uint8_t> payload(uint32_t i, size_t len) {
  std::vector<uint8_t> v(len + 4);
  for (int k = 0; k < 4; k++) v[k] = (uint8_t)(i >> (8 * k));
  uint32_t x = i * 2654435761u + 1;
  for (size_t k = 0; k < len; k++) {
    x = x * 1664525u + 1013904223u;
    v[4 + k] = (uint8_t)(x >> 24);
  }
  return v;
}


static void testRoomCodes() {
  std::set<std::string> seen;
  std::map<char, int> freq;
  for (int i = 0; i < 300; i++) {
    std::string c = newRoomCode();
    CHECK(c.size() == 6);
    CHECK(normalizeRoomCode(c) == c);
    CHECK(normalizeRoomCode(formatRoomCode(c)) == c);
    CHECK(formatRoomCode(c).size() == 7 && formatRoomCode(c)[3] == '-');
    std::string lower = c;
    for (auto& ch : lower) ch = (char)tolower(ch);
    CHECK(normalizeRoomCode(lower) == c);
    for (char ch : c) {
      CHECK(std::strchr(kRoomAlphabet, ch) != nullptr);
      freq[ch]++;
    }
    seen.insert(c);
  }
  CHECK(seen.size() == 300);
  CHECK(freq.size() == 31);
  CHECK(std::strlen(kRoomAlphabet) == 31 && !std::strchr(kRoomAlphabet, '0') && !std::strchr(kRoomAlphabet, 'O') &&
        !std::strchr(kRoomAlphabet, '1') && !std::strchr(kRoomAlphabet, 'I') && !std::strchr(kRoomAlphabet, 'L'));
  CHECK(normalizeRoomCode("").empty());
  CHECK(normalizeRoomCode("ABC-DE").empty());
  CHECK(normalizeRoomCode("ABC-DEFG").empty());
  CHECK(normalizeRoomCode("ABC-DE0").empty());
  CHECK(normalizeRoomCode("abc def") == "ABCDEF");
  uint8_t h1[20], h2[20], h3[20];
  std::string c = newRoomCode(), d = newRoomCode();
  roomInfohash(c, h1);
  roomInfohash(c, h2);
  roomInfohash(d, h3);
  CHECK(std::memcmp(h1, h2, 20) == 0);
  CHECK(std::memcmp(h1, h3, 20) != 0);
}

static void testCrypto() {
  std::string c = newRoomCode(), other = newRoomCode();
  Pake a(c, 1), b(c, 1), w(other, 1), v2(c, 2);
  SessionKeys ka, kb, kw, kv;
  CHECK(a.finish(b.publicValue(), ka));
  CHECK(b.finish(a.publicValue(), kb));
  CHECK(std::memcmp(ka.sendKey, kb.recvKey, 32) == 0);
  CHECK(std::memcmp(ka.recvKey, kb.sendKey, 32) == 0);
  CHECK(std::memcmp(ka.transcript, kb.transcript, 32) == 0);
  CHECK(std::memcmp(ka.sendKey, ka.recvKey, 32) != 0);

  CHECK(a.finish(w.publicValue(), ka));
  CHECK(w.finish(a.publicValue(), kw));
  CHECK(std::memcmp(ka.sendKey, kw.recvKey, 32) != 0);

  CHECK(a.finish(v2.publicValue(), ka));
  CHECK(v2.finish(a.publicValue(), kv));
  CHECK(std::memcmp(ka.sendKey, kv.recvKey, 32) != 0);

  CHECK(!a.finish(a.publicValue(), ka));
  uint8_t zero[32] = {};
  CHECK(!a.finish(zero, ka));
  uint8_t one[32] = {1};
  CHECK(!a.finish(one, ka));

  uint8_t key[32];
  randomBytes(key, 32);
  uint8_t pt[40], ct[56], back[40];
  randomBytes(pt, 40);
  uint8_t ad[3] = {1, 2, 3};
  aeadSeal(key, 7, ad, 3, pt, 40, ct);
  CHECK(aeadOpen(key, 7, ad, 3, ct, 56, back) && std::memcmp(back, pt, 40) == 0);
  CHECK(!aeadOpen(key, 8, ad, 3, ct, 56, back));
  ad[0] = 9;
  CHECK(!aeadOpen(key, 7, ad, 3, ct, 56, back));
  ad[0] = 1;
  for (int i = 0; i < 56; i++) {
    ct[i] ^= 0x01;
    CHECK(!aeadOpen(key, 7, ad, 3, ct, 56, back));
    ct[i] ^= 0x01;
  }
}

static void testProto() {
  uint8_t tid[12];
  randomBytes(tid, 12);
  auto req = stunBuildRequest(tid);
  CHECK(req.size() == 20 && req[0] == 0 && req[1] == 1);

  std::vector<uint8_t> r = {0x01, 0x01, 0x00, 0x0C, 0x21, 0x12, 0xA4, 0x42};
  r.insert(r.end(), tid, tid + 12);
  uint16_t xp = 54321 ^ 0x2112;
  uint32_t xa = ((203u << 24) | (0u << 16) | (113u << 8) | 9u) ^ 0x2112A442u;
  std::vector<uint8_t> attr = {0x00, 0x20, 0x00, 0x08, 0x00, 0x01, (uint8_t)(xp >> 8), (uint8_t)xp,
                               (uint8_t)(xa >> 24), (uint8_t)(xa >> 16), (uint8_t)(xa >> 8), (uint8_t)xa};
  r.insert(r.end(), attr.begin(), attr.end());
  Endpoint m;
  CHECK(stunParseResponse(r.data(), r.size(), tid, m));
  CHECK(m == Endpoint::v4(203, 0, 113, 9, 54321));
  uint8_t other[12] = {};
  CHECK(!stunParseResponse(r.data(), r.size(), other, m));
  CHECK(!stunParseResponse(r.data(), 10, tid, m));
  r.resize(r.size() - 3);
  CHECK(!stunParseResponse(r.data(), r.size(), tid, m));

  auto c = trackerBuildConnect(0xAABBCCDD);
  CHECK(c.size() == 16 && c[7] == 0x80 && c[15] == 0xDD);
  uint8_t cr[16] = {0, 0, 0, 0, 0xAA, 0xBB, 0xCC, 0xDD, 0, 0, 0, 0, 0, 0, 0, 7};
  uint64_t id = 0;
  CHECK(trackerParseConnect(cr, 16, 0xAABBCCDD, id) && id == 7);
  CHECK(!trackerParseConnect(cr, 16, 1, id));
  uint8_t ih[20] = {1}, pid[20] = {2};
  auto a = trackerBuildAnnounce(7, 99, ih, pid, kEvStarted, 5, 4000);
  CHECK(a.size() == 98 && a[96] == 0x0F && a[97] == 0xA0);
  uint8_t ar[32] = {0, 0, 0, 1, 0, 0, 0, 99, 0, 0, 7, 8, 0, 0, 0, 1, 0, 0, 0, 1, 1, 2, 3, 4, 0x1F, 0x90, 5, 6, 7, 8, 0, 80};
  uint32_t iv;
  std::vector<Endpoint> peers;
  CHECK(trackerParseAnnounce(ar, 32, 99, iv, peers) && iv == 1800 && peers.size() == 2);
  CHECK(peers[0] == Endpoint::v4(1, 2, 3, 4, 8080) && peers[1] == Endpoint::v4(5, 6, 7, 8, 80));
  CHECK(!trackerParseAnnounce(ar, 32, 98, iv, peers));
  CHECK(!trackerParseAnnounce(ar, 19, 99, iv, peers));
  Endpoint e;
  CHECK(Endpoint::parse("1.2.3.4:5", e) && e.str() == "1.2.3.4:5");
  CHECK(!Endpoint::parse("1.2.3.4", e) && !Endpoint::parse("1.2.3.256:5", e) && !Endpoint::parse("1.2.3.4:0", e));
}

static void testBasicSession() {
  Sim n;
  std::string code = newRoomCode();
  SessionConfig ca, cb;
  ca.fingerprint = {1, 2, 3, 4};
  cb.fingerprint = {9, 8, 7};
  n.make(code, code, ca, cb);
  CHECK(n.connect());
  CHECK(n.s[0]->peerFingerprint() == cb.fingerprint);
  CHECK(n.s[1]->peerFingerprint() == ca.fingerprint);
  CHECK(n.s[0]->isLowSide() != n.s[1]->isLowSide());
  CHECK(n.s[0]->peer() == n.ep[1]);

  const int N = 300;
  std::vector<std::vector<uint8_t>> sent;
  for (int i = 0; i < N; i++) {
    size_t len = i % 50 == 0 ? 60000 : (i % 7 == 0 ? 0 : (size_t)(i * 13 % 1500));
    sent.push_back(payload(i, len));
    CHECK(n.s[0]->sendReliable(sent.back().data(), sent.back().size()));
    CHECK(n.s[1]->sendReliable(sent.back().data(), sent.back().size()));
  }
  CHECK(!n.s[0]->sendReliable(sent[0].data(), kMaxMessage + 1));
  int got[2] = {0, 0};
  bool order = true, content = true;
  n.run(60000, [&] {
    for (int k = 0; k < 2; k++) {
      std::vector<uint8_t> m;
      bool rel;
      while (n.s[k]->receive(m, &rel)) {
        if (!rel) continue;
        if (got[k] >= N || m != sent[got[k]]) content = false;
        got[k]++;
      }
    }
    return got[0] == N && got[1] == N;
  });
  CHECK(got[0] == N && got[1] == N);
  CHECK(order && content);

  uint8_t u[5] = {5, 4, 3, 2, 1};
  CHECK(n.s[0]->sendUnreliable(u, 5));
  CHECK(!n.s[0]->sendUnreliable(u, kMaxUnreliable + 1));
  bool gotU = false;
  n.run(1000, [&] {
    std::vector<uint8_t> m;
    bool rel = true;
    while (n.s[1]->receive(m, &rel))
      if (!rel && m.size() == 5) gotU = true;
    return gotU;
  });
  CHECK(gotU);

  n.run(3000, [] { return false; });
  CHECK(n.s[0]->stats().rttMs >= 35 && n.s[0]->stats().rttMs <= 60);

  n.s[0]->close();
  CHECK(n.s[0]->state() == SessionState::Closed && n.s[0]->closeReason() == CloseReason::Local);
  n.run(1000, [&] { return n.s[1]->state() == SessionState::Closed; });
  CHECK(n.s[1]->state() == SessionState::Closed && n.s[1]->closeReason() == CloseReason::Remote);
  CHECK(!n.s[1]->sendReliable(u, 5));
}

static void testBadNetwork() {
  for (double loss : {0.1, 0.3}) {
    Sim n;
    n.loss = loss;
    n.dup = 0.1;
    n.jitterMs = 60;
    std::string code = newRoomCode();
    n.make(code, code);
    CHECK(n.connect(60000));
    const int N = 400;
    std::vector<std::vector<uint8_t>> sent;
    for (int i = 0; i < N; i++) {
      sent.push_back(payload(i, (size_t)(i * 31 % 2500)));
      while (!n.s[0]->sendReliable(sent.back().data(), sent.back().size())) n.step();
    }
    int got = 0;
    bool ok = true;
    n.run(300000, [&] {
      std::vector<uint8_t> m;
      bool rel;
      while (n.s[1]->receive(m, &rel))
        if (rel) {
          if (got >= N || m != sent[got]) ok = false;
          got++;
        }
      return got == N;
    });
    CHECK(got == N && ok);
    CHECK(n.s[0]->stats().retransmits > 0);
    std::printf("  loss %.0f%%: %d messages in order, %llu retransmits, rtt %u ms\n", loss * 100, got,
                (unsigned long long)n.s[0]->stats().retransmits, n.s[0]->stats().rttMs);
  }
}

static void testAttacks() {
  std::string code = newRoomCode();

  {
    Sim n;
    n.make(code, newRoomCode());
    CHECK(!n.connect(8000));
    CHECK(n.s[0]->state() == SessionState::Handshaking && n.s[1]->state() == SessionState::Handshaking);
    CHECK(n.s[0]->issue() == Issue::WrongCodeOrTampered);
    CHECK(n.s[0]->stats().rejected > 0);
  }

  {
    Sim n;
    SessionConfig a, b;
    b.version = 2;
    n.make(code, code, a, b);
    CHECK(!n.connect(3000));
    CHECK(n.s[0]->issue() == Issue::PeerVersionDiffers);
  }

  for (int pos : {1, 2, 3, 10, 20, 34}) {
    Sim n;
    n.tamper = [pos](int, std::vector<uint8_t>& v) {
      if ((size_t)pos < v.size() && (v[0] != 0xA1 || pos < 35)) v[pos] ^= 0x10;
    };
    n.make(code, code);
    CHECK(!n.connect(5000));
  }

  {
    Sim n;
    n.make(code, code);
    CHECK(n.connect());
    n.tamper = [](int, std::vector<uint8_t>& v) {
      if (v[0] == 0xA3) v[v.size() - 1] ^= 1;
    };
    uint8_t m[3] = {1, 2, 3};
    n.s[0]->sendReliable(m, 3);
    n.run(15000, [&] { return n.s[1]->state() == SessionState::Closed; });
    std::vector<uint8_t> out;
    CHECK(!n.s[1]->receive(out));
    CHECK(n.s[1]->state() == SessionState::Closed && n.s[1]->closeReason() == CloseReason::Timeout);
    CHECK(n.s[1]->stats().rejected > 0);
  }

  {
    Sim n;
    n.make(code, code);
    CHECK(n.connect());
    for (int i = 0; i < 20; i++) {
      auto p = payload(i, 100);
      n.s[0]->sendReliable(p.data(), p.size());
    }
    int got = 0;
    n.run(3000, [&] {
      std::vector<uint8_t> m;
      while (n.s[1]->receive(m)) got++;
      return false;
    });
    CHECK(got == 20);
    uint64_t rejBefore = n.s[1]->stats().rejected;
    size_t replays = 0;
    for (auto& p : n.seenData) {
      n.s[1]->onDatagram(n.ep[0], p.data(), p.size(), n.now);
      replays++;
    }
    std::vector<uint8_t> m;
    CHECK(!n.s[1]->receive(m));
    CHECK(n.s[1]->stats().rejected >= rejBefore + replays / 2);
  }

  {
    Sim n;
    n.make(code, code);
    std::mt19937 r(99);
    for (int i = 0; i < 20000; i++) {
      size_t len = r() % 1300;
      std::vector<uint8_t> v(len);
      for (auto& b : v) b = (uint8_t)r();
      if (len && i % 3 == 0) v[0] = (uint8_t)(0xA1 + r() % 3);
      if (i % 9 == 0 && len >= 600) v.resize(kHelloSize), v[0] = 0xA1, v[1] = 0, v[2] = 1;
      n.s[1]->onDatagram(Endpoint::v4(6, 6, 6, (uint8_t)(r() % 200), 666), v.data(), v.size(), n.now + i);
    }
    CHECK(n.s[1]->state() != SessionState::Connected);
    CHECK(n.connect(10000));
  }

  {
    Sim n;
    n.make(code, code);
    uint8_t junk[40] = {0xA3};
    for (int i = 0; i < 20000; i++) n.s[0]->onDatagram(n.ep[1], junk, sizeof junk, n.now);
    CHECK(n.s[0]->stats().rejected >= 20000 - 4000);
  }


  {
    Sim n;
    n.make(code, code);
    n.blackhole = true;
    auto attempt = [&](const Endpoint& who) {
      Pake wrong(newRoomCode(), 1);
      std::vector<uint8_t> hello(kHelloSize, 0);
      hello[0] = 0xA1;
      hello[2] = 1;
      std::memcpy(hello.data() + 3, wrong.publicValue(), 32);
      n.now += 200;
      uint64_t before = n.sent;
      n.s[0]->onDatagram(who, hello.data(), hello.size(), n.now);
      bool answered = n.sent > before;
      std::vector<uint8_t> conf(1 + 4 + 16 + 3, 0x5A);
      conf[0] = 0xA2;
      n.s[0]->onDatagram(who, conf.data(), conf.size(), n.now);
      return answered;
    };
    Endpoint one = Endpoint::v4(9, 9, 9, 1, 5000);
    for (int k = 0; k < 3; k++) CHECK(attempt(one));
    CHECK(n.s[0]->stats().authFailures == 3 && n.s[0]->stats().blockedAddresses == 1);
    CHECK(!attempt(one));
    CHECK(n.s[0]->stats().authFailures == 3);
    for (int i = 2; i <= 4; i++)
      for (int k = 0; k < 3; k++) attempt(Endpoint::v4(9, 9, 9, (uint8_t)i, 5000));
    CHECK(n.s[0]->stats().authFailures == 12);
    CHECK(!attempt(Endpoint::v4(9, 9, 9, 77, 5000)));
    CHECK(n.s[0]->stats().authFailures == 12);
    n.blackhole = false;
    CHECK(n.connect(10000));
  }

  {
    Sim n;
    n.make(code, code);
    CHECK(n.connect());
    n.blackhole = true;
    uint64_t t0 = n.now;
    n.run(30000, [&] { return n.s[0]->state() == SessionState::Closed; });
    CHECK(n.s[0]->closeReason() == CloseReason::Timeout);
    CHECK(n.now - t0 >= 9000 && n.now - t0 <= 12000);
  }

  {
    Sim n;
    SessionConfig a;
    a.handshakeTimeoutMs = 3000;
    n.make(code, code, a, a);
    n.blackhole = true;
    n.run(10000, [&] { return n.s[0]->state() == SessionState::Closed; });
    CHECK(n.s[0]->closeReason() == CloseReason::HandshakeTimeout);
  }

  {
    Sim n;
    n.make(code, code);
    uint64_t before = n.sent;
    uint8_t shortHello[100] = {0xA1, 0, 1};
    n.s[1]->onDatagram(n.ep[0], shortHello, sizeof shortHello, n.now);
    CHECK(n.sent == before);
  }
}





struct Nat {
  uint8_t pubIp;
  bool symmetric, portRestricted;
  uint16_t conePort;
  uint16_t nextPort;
  std::map<std::pair<uint32_t, uint16_t>, uint16_t> symPort;
  std::set<std::pair<uint32_t, uint16_t>> contacted;
  uint16_t stunPort;
};
static uint32_t ipKey(const Endpoint& e) { return (uint32_t)e.ip[0] << 24 | e.ip[1] << 16 | e.ip[2] << 8 | e.ip[3]; }
static Endpoint pubEp(uint8_t ip, uint16_t port) { return Endpoint::v4(ip, 0, 0, ip, port); }

static bool natConnects(bool symA, bool restrictA, bool symB, bool restrictB) {
  Nat nat[2] = {{1, symA, restrictA, 31000, 41000, {}, {}, 0}, {2, symB, restrictB, 32000, 42000, {}, {}, 0}};
  for (auto& n : nat) n.stunPort = n.symmetric ? (uint16_t)(n.conePort + 777) : n.conePort;
  std::string code = newRoomCode();
  struct Pkt {
    uint64_t at;
    int to;
    Endpoint addressedTo, from;
    std::vector<uint8_t> d;
  };
  std::vector<Pkt> q;
  uint64_t now = 1000;
  std::unique_ptr<Session> s[2];
  for (int i = 0; i < 2; i++) {
    s[i].reset(new Session(code, {}, [&, i](const Endpoint& to, const uint8_t* d, size_t n) {
      Nat& me = nat[i];
      uint16_t srcPort = me.conePort;
      if (me.symmetric) {
        auto key = std::make_pair(ipKey(to), to.port);
        auto it = me.symPort.find(key);
        if (it == me.symPort.end()) it = me.symPort.insert({key, me.nextPort++}).first;
        srcPort = it->second;
      }
      me.contacted.insert({ipKey(to), to.port});
      if (to.ip[0] != nat[1 - i].pubIp) return;
      q.push_back({now + 15, 1 - i, to, pubEp(me.pubIp, srcPort), std::vector<uint8_t>(d, d + n)});
    }));
  }
  for (int i = 0; i < 2; i++) s[i]->addCandidate(pubEp(nat[1 - i].pubIp, nat[1 - i].stunPort));
  for (int t = 0; t < 10000; t += 5) {
    now += 5;
    for (size_t k = 0; k < q.size();) {
      if (q[k].at > now) {
        k++;
        continue;
      }
      Pkt p = std::move(q[k]);
      q.erase(q.begin() + (long)k);
      Nat& dst = nat[p.to];
      bool mappedPort = dst.symmetric ? false : p.addressedTo.port == dst.conePort;
      if (dst.symmetric)
        for (auto& kv : dst.symPort) mappedPort = mappedPort || kv.second == p.addressedTo.port;
      if (!mappedPort) continue;
      bool allowed = dst.portRestricted ? dst.contacted.count({ipKey(p.from), p.from.port}) > 0
                                        : [&] {
                                            for (auto& c : dst.contacted)
                                              if (c.first == ipKey(p.from)) return true;
                                            return false;
                                          }();
      if (!allowed) continue;
      s[p.to]->onDatagram(p.from, p.d.data(), p.d.size(), now);
    }
    s[0]->update(now);
    s[1]->update(now);
    if (s[0]->state() == SessionState::Connected && s[1]->state() == SessionState::Connected) {
      uint8_t m[2] = {1, 2};
      s[0]->sendReliable(m, 2);
      for (int k = 0; k < 400; k++) {
        now += 5;
        for (size_t j = 0; j < q.size();) {
          if (q[j].at > now) {
            j++;
            continue;
          }
          Pkt p = std::move(q[j]);
          q.erase(q.begin() + (long)j);
          Nat& dst = nat[p.to];
          bool mappedPort = !dst.symmetric && p.addressedTo.port == dst.conePort;
          for (auto& kv : dst.symPort) mappedPort = mappedPort || kv.second == p.addressedTo.port;
          if (mappedPort) s[p.to]->onDatagram(p.from, p.d.data(), p.d.size(), now);
        }
        s[0]->update(now);
        s[1]->update(now);
        std::vector<uint8_t> out;
        if (s[1]->receive(out)) return out.size() == 2;
      }
      return false;
    }
  }
  return false;
}

static void testNatPunching() {
  CHECK(natConnects(false, true, false, true));
  CHECK(natConnects(false, false, false, false));
  CHECK(natConnects(false, false, true, true));
  CHECK(natConnects(true, true, false, false));
  CHECK(!natConnects(false, true, true, true));
  CHECK(!natConnects(true, true, true, true));
}
static void testConnectionBadCode() {
  Connection c;
  CHECK(!c.start("NOT A CODE", Connection::defaultConfig()));
  auto s = c.status();
  CHECK(s.phase == Phase::Failed && s.fail == Fail::BadCode);
}

int main() {
  std::printf("net_test\n");
  testRoomCodes();
  testCrypto();
  testProto();
  testBasicSession();
  testBadNetwork();
  testAttacks();
  testNatPunching();
  testConnectionBadCode();
  std::printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
