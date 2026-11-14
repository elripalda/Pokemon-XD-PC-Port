#include "net_udp.h"

#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
typedef int socklen_t_;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
typedef socklen_t socklen_t_;
#endif

namespace xdnet {

#if defined(_WIN32)
static void netInit() {
  static bool done = [] {
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0;
  }();
  (void)done;
}
#else
static void netInit() {}
#endif

bool UdpSocket::open(uint16_t localPort) {
  close();
  netInit();
  long long s = (long long)::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#if defined(_WIN32)
  if ((SOCKET)s == INVALID_SOCKET) return false;
  u_long nb = 1;
  ioctlsocket((SOCKET)s, FIONBIO, &nb);
  BOOL off = FALSE;
  DWORD ret = 0;
  WSAIoctl((SOCKET)s, SIO_UDP_CONNRESET, &off, sizeof off, nullptr, 0, &ret, nullptr, nullptr);
#else
  if (s < 0) return false;
  int fl = fcntl((int)s, F_GETFL, 0);
  fcntl((int)s, F_SETFL, fl | O_NONBLOCK);
#endif
  sockaddr_in a;
  std::memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(localPort);
  if (::bind((
#if defined(_WIN32)
                 SOCKET
#else
                 int
#endif
                 )s,
             (sockaddr*)&a, sizeof a) != 0) {
#if defined(_WIN32)
    closesocket((SOCKET)s);
#else
    ::close((int)s);
#endif
    return false;
  }
  socklen_t_ len = sizeof a;
  getsockname(
#if defined(_WIN32)
      (SOCKET)s,
#else
      (int)s,
#endif
      (sockaddr*)&a, &len);
  port_ = ntohs(a.sin_port);
  fd_ = s;
  return true;
}

void UdpSocket::close() {
  if (fd_ == -1) return;
#if defined(_WIN32)
  closesocket((SOCKET)fd_);
#else
  ::close((int)fd_);
#endif
  fd_ = -1;
}

static double testLossPercent() {
  static const double v = [] {
    const char* e = std::getenv("XD_NET_TEST_LOSS");
    return e ? std::atof(e) : 0.0;
  }();
  return v;
}

bool UdpSocket::sendTo(const Endpoint& to, const void* data, size_t size) {
  if (fd_ == -1 || to.family != 4) return false;
  if (testLossPercent() > 0) {
    uint32_t r;
    randomBytes(&r, 4);
    if ((double)(r % 10000) / 100.0 < testLossPercent()) return true;
  }
  sockaddr_in a;
  std::memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  std::memcpy(&a.sin_addr, to.ip, 4);
  a.sin_port = htons(to.port);
  int r = (int)::sendto(
#if defined(_WIN32)
      (SOCKET)fd_,
#else
      (int)fd_,
#endif
      (const char*)data, (int)size, 0, (sockaddr*)&a, sizeof a);
  return r == (int)size;
}

int UdpSocket::recvFrom(Endpoint& from, void* buf, size_t cap) {
  if (fd_ == -1) return -1;
  sockaddr_in a;
  socklen_t_ len = sizeof a;
  int r = (int)::recvfrom(
#if defined(_WIN32)
      (SOCKET)fd_,
#else
      (int)fd_,
#endif
      (char*)buf, (int)cap, 0, (sockaddr*)&a, &len);
  if (r < 0) return -1;
  from = Endpoint();
  from.family = 4;
  std::memcpy(from.ip, &a.sin_addr, 4);
  from.port = ntohs(a.sin_port);
  return r;
}

bool UdpSocket::waitReadable(uint32_t timeoutMs) {
  if (fd_ == -1) return false;
  fd_set rs;
  FD_ZERO(&rs);
#if defined(_WIN32)
  FD_SET((SOCKET)fd_, &rs);
#else
  FD_SET((int)fd_, &rs);
#endif
  timeval tv;
  tv.tv_sec = (long)(timeoutMs / 1000);
  tv.tv_usec = (long)(timeoutMs % 1000) * 1000;
  int r = ::select((int)fd_ + 1, &rs, nullptr, nullptr, &tv);
  return r > 0;
}

bool UdpSocket::resolve(const std::string& host, uint16_t port, Endpoint& out) {
  netInit();
  addrinfo hints;
  std::memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
  bool ok = false;
  for (addrinfo* p = res; p; p = p->ai_next) {
    if (p->ai_family == AF_INET) {
      out = Endpoint();
      out.family = 4;
      std::memcpy(out.ip, &((sockaddr_in*)p->ai_addr)->sin_addr, 4);
      out.port = port;
      ok = true;
      break;
    }
  }
  freeaddrinfo(res);
  return ok;
}

bool UdpSocket::resolveHostPort(const std::string& hp, Endpoint& out) {
  size_t c = hp.rfind(':');
  if (c == std::string::npos || c == 0) return false;
  int port = std::atoi(hp.c_str() + c + 1);
  if (port <= 0 || port > 65535) return false;
  return resolve(hp.substr(0, c), (uint16_t)port, out);
}

}
