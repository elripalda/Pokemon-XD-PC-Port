#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "net_connection.h"
#include "net_crypto.h"
#include "net_proto.h"

using namespace xdnet;

static std::vector<std::string> splitList(const std::string& s) {
  std::vector<std::string> v;
  size_t i = 0;
  while (i <= s.size()) {
    size_t j = s.find(',', i);
    if (j == std::string::npos) j = s.size();
    if (j > i) v.push_back(s.substr(i, j - i));
    i = j + 1;
  }
  return v;
}

static const char* natName(NatKind k) {
  switch (k) {
    case NatKind::EndpointIndependent: return "endpoint-independent (good for hole punching)";
    case NatKind::Symmetric: return "symmetric (hard: may not connect)";
    case NatKind::NoReply: return "no STUN reply";
    default: return "unknown";
  }
}

static void printStats(const ConnectionStatus& s) {
  std::printf("[stats] rtt %u ms (last %u) jitter %u ms | sent %llu recv %llu lost %llu retrans %llu rejected %llu | %llu/%llu bytes\n",
              s.stats.rttMs, s.stats.rttLastMs, s.stats.jitterMs, (unsigned long long)s.stats.packetsSent,
              (unsigned long long)s.stats.packetsReceived, (unsigned long long)s.stats.packetsLost,
              (unsigned long long)s.stats.retransmits, (unsigned long long)s.stats.rejected,
              (unsigned long long)s.stats.bytesSent, (unsigned long long)s.stats.bytesReceived);
  std::fflush(stdout);
}

static std::vector<uint8_t> soakPayload(uint32_t i) {
  size_t len = i % 25 == 0 ? 20000 : (size_t)(i * 37 % 1200);
  std::vector<uint8_t> v(8 + len);
  for (int k = 0; k < 4; k++) v[k] = (uint8_t)(i >> (8 * k));
  for (int k = 0; k < 4; k++) v[4 + k] = (uint8_t)(len >> (8 * k));
  uint32_t x = i * 2654435761u + 7;
  for (size_t k = 0; k < len; k++) {
    x = x * 1664525u + 1013904223u;
    v[8 + k] = (uint8_t)(x >> 24);
  }
  return v;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: xd_net_test host|join <code> [--mode chat|ping|soak] [--seconds N] [--direct ip:port] ...\n");
    return 2;
  }
  std::string cmd = argv[1], code, mode = "chat";
  int seconds = 10;
  ConnectionConfig cfg = Connection::defaultConfig();
  int ai = 2;
  if (cmd == "join") {
    if (argc < 3) return 2;
    code = argv[2];
    ai = 3;
  } else if (cmd != "host") {
    std::printf("unknown command %s\n", argv[1]);
    return 2;
  }
  for (int i = ai; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--mode") mode = next();
    else if (a == "--seconds") seconds = std::atoi(next().c_str());
    else if (a == "--direct") cfg.directPeers.push_back(next());
    else if (a == "--no-go-between") cfg.useGoBetween = false;
    else if (a == "--local-port") cfg.localPort = (uint16_t)std::atoi(next().c_str());
    else if (a == "--code") code = next();
    else if (a == "--trackers") cfg.trackers = splitList(next());
    else if (a == "--stun") cfg.stunServers = splitList(next());
    else if (a == "--find-timeout") cfg.findFriendTimeoutMs = (uint32_t)std::atoi(next().c_str()) * 1000;
    else {
      std::printf("unknown option %s\n", a.c_str());
      return 2;
    }
  }
  if (cmd == "host" && code.empty()) code = newRoomCode();
  cfg.session.fingerprint = {'x', 'd', 'n', 't', '1'};

  Connection c;
  if (!c.start(code, cfg)) {
    std::printf("%s\n", c.status().text.c_str());
    return 3;
  }
  std::printf("ROOM CODE: %s\n", c.status().roomCode.c_str());
  std::fflush(stdout);

  Phase last = Phase::Idle;
  std::string lastText;
  std::string lastMapped;
  uint64_t t0 = nowMs();
  while (true) {
    ConnectionStatus s = c.status();
    if (s.phase != last || s.text != lastText) {
      std::printf("[%5.1fs] %s: %s\n", (double)(nowMs() - t0) / 1000.0, phaseName(s.phase), s.text.c_str());
      last = s.phase;
      lastText = s.text;
    }
    if (s.mappedMasked != lastMapped) {
      lastMapped = s.mappedMasked;
      std::printf("[net] the internet sees this PC as %s (%s); %d STUN answers\n", s.mappedMasked.c_str(), natName(s.nat),
                  s.stunAnswers);
    }
    std::fflush(stdout);
    if (s.phase == Phase::Connected) break;
    if (s.phase == Phase::Failed || s.phase == Phase::Closed) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ConnectionStatus s = c.status();
  std::printf("[net] connected to %s in %.1fs; %s; friend's fingerprint %zu bytes; trackers answering %d\n",
              s.peerMasked.c_str(), (double)(nowMs() - t0) / 1000.0, s.low ? "we are LOW (player 1)" : "we are HIGH (player 2)",
              s.peerFingerprint.size(), s.trackersAnswering);
  std::fflush(stdout);

  int rc = 0;
  if (mode == "ping") {
    for (int i = 0; i < seconds; i++) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      s = c.status();
      printStats(s);
      if (s.phase != Phase::Connected) {
        rc = 1;
        break;
      }
    }
  } else if (mode == "soak") {
    uint32_t nextSend = 0, nextRecv = 0, bad = 0, unreliableGot = 0;
    bool sentDone = false, gotDone = false, sentAck = false, gotAck = false;
    uint64_t start = nowMs(), end = start + (uint64_t)seconds * 1000, lastPrint = start, finishAt = 0;
    while (true) {
      s = c.status();
      uint64_t now = nowMs();
      if (s.phase != Phase::Connected) {
        if (!(sentAck && gotDone && bad == 0)) {
          std::printf("connection ended during the soak: %s\n", s.text.c_str());
          rc = 1;
        }
        break;
      }
      if (now < end) {
        for (int k = 0; k < 5; k++) {
          auto p = soakPayload(nextSend);
          p.insert(p.begin(), 'M');
          if (!c.send(p.data(), p.size(), true)) break;
          nextSend++;
        }
        uint8_t u[16] = {'U', 2, 3};
        c.send(u, sizeof u, false);
      } else if (!sentDone) {
        uint8_t d[5] = {'D'};
        for (int k = 0; k < 4; k++) d[1 + k] = (uint8_t)(nextSend >> (8 * k));
        sentDone = c.send(d, sizeof d, true);
      } else if (now > end + 60000) {
        std::printf("soak: gave up waiting for the friend (received %u)\n", nextRecv);
        rc = 1;
        break;
      }
      std::vector<uint8_t> m;
      bool rel;
      while (c.receive(m, &rel)) {
        if (m.empty()) continue;
        if (!rel) {
          unreliableGot++;
        } else if (m[0] == 'M') {
          std::vector<uint8_t> body(m.begin() + 1, m.end());
          auto want = soakPayload(nextRecv);
          if (body != want) {
            bad++;
            std::printf("soak: message %u wrong (size %zu, expected %zu)\n", nextRecv, body.size(), want.size());
          }
          nextRecv++;
        } else if (m[0] == 'D' && m.size() == 5) {
          uint32_t cnt = m[1] | m[2] << 8 | m[3] << 16 | (uint32_t)m[4] << 24;
          gotDone = true;
          if (cnt != nextRecv) {
            bad++;
            std::printf("soak: friend sent %u messages, %u arrived\n", cnt, nextRecv);
          }
          uint8_t a[1] = {'A'};
          sentAck = c.send(a, 1, true);
        } else if (m[0] == 'A') {
          gotAck = true;
        }
      }
      if (sentAck && gotAck && finishAt == 0) finishAt = now + 1500;
      if (finishAt && now >= finishAt) break;
      if (now - lastPrint > 2000) {
        lastPrint = now;
        std::printf("[soak] sent %u received %u bad %u unreliable %u\n", nextSend, nextRecv, bad, unreliableGot);
        printStats(s);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    s = c.status();
    printStats(s);
    bool ok = rc == 0 && bad == 0 && gotDone && gotAck && nextRecv > 0;
    std::printf("[soak] RESULT sent %u received %u bad %u unreliable %u -> %s\n", nextSend, nextRecv, bad, unreliableGot,
                ok ? "OK" : "FAILED");
    if (!ok) rc = 1;
  } else {
    std::atomic<bool> quit{false};
    std::thread reader([&] {
      std::string line;
      while (!quit && std::getline(std::cin, line)) {
        if (line == "/quit") break;
        c.send(line);
      }
      quit = true;
    });
    reader.detach();
    uint64_t end = seconds > 0 && argc > 0 && mode == "chat" ? 0 : 0;
    (void)end;
    while (!quit) {
      std::vector<uint8_t> m;
      bool rel;
      while (c.receive(m, &rel)) std::printf("friend%s: %.*s\n", rel ? "" : " (unreliable)", (int)m.size(), (const char*)m.data());
      std::fflush(stdout);
      s = c.status();
      if (s.phase != Phase::Connected) {
        std::printf("%s\n", s.text.c_str());
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  c.close();
  return rc;
}
