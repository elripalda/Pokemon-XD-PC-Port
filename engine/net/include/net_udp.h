#pragma once
#include <string>

#include "net_types.h"

namespace xdnet {

class UdpSocket {
 public:
  UdpSocket() = default;
  ~UdpSocket() { close(); }
  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

  bool open(uint16_t localPort = 0);
  void close();
  bool isOpen() const { return fd_ != -1; }
  uint16_t localPort() const { return port_; }
  bool sendTo(const Endpoint& to, const void* data, size_t size);
  int recvFrom(Endpoint& from, void* buf, size_t cap);

  bool waitReadable(uint32_t timeoutMs);

  static bool resolve(const std::string& host, uint16_t port, Endpoint& out);
  static bool resolveHostPort(const std::string& hostPort, Endpoint& out);

 private:
  long long fd_ = -1;
  uint16_t port_ = 0;
};

}
