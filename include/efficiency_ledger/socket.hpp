#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace el {

// RAII scoped socket. All I/O is partial-read/partial-write safe.
class Socket {
 public:
  Socket() = default;
  ~Socket() { close(); }
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  Socket(Socket&& o) noexcept : fd_(o.fd_) { o.fd_ = kInvalidSocket; }
  Socket& operator=(Socket&& o) noexcept {
    if (this != &o) { close(); fd_ = o.fd_; o.fd_ = kInvalidSocket; }
    return *this;
  }

  bool valid() const {
    return fd_ != kInvalidSocket;
  }
  bool bind_and_listen(std::uint16_t port);
  bool accept(Socket& out);
  bool connect_to(const char* host, std::uint16_t port);
  bool send_all(const std::string& data);
  bool recv_all(std::string& out, std::size_t len);
  void close();

  SocketHandle handle() const { return fd_; }
  void adopt(SocketHandle h) { fd_ = h; }

 private:
  SocketHandle fd_ = kInvalidSocket;
};

inline bool Socket::bind_and_listen(std::uint16_t port) {
#ifdef _WIN32
  fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd_ == kInvalidSocket) return false;
  int opt = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
  if (listen(fd_, 16) != 0) return false;
  return true;
#else
  fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd_ == kInvalidSocket) return false;
  int opt = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
  if (listen(fd_, 16) != 0) return false;
  return true;
#endif
}

inline bool Socket::accept(Socket& out) {
#ifdef _WIN32
  sockaddr_in peer{};
  int n = static_cast<int>(sizeof(peer));
  SOCKET c = ::accept(fd_, reinterpret_cast<sockaddr*>(&peer), &n);
  if (c == INVALID_SOCKET) return false;
  out.adopt(c);
  return true;
#else
  sockaddr_in peer{};
  socklen_t n = sizeof(peer);
  int c = ::accept(fd_, reinterpret_cast<sockaddr*>(&peer), &n);
  if (c == -1) return false;
  out.adopt(c);
  return true;
#endif
}

inline bool Socket::connect_to(const char* host, std::uint16_t port) {
#ifdef _WIN32
  fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd_ == kInvalidSocket) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
    hostent* he = gethostbyname(host);
    if (!he) return false;
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
  }
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) return true;
    Sleep(30);
  }
  return false;
#else
  fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd_ == kInvalidSocket) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, host, &addr.sin_addr);
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) return true;
    usleep(30000);
  }
  return false;
#endif
}

inline bool Socket::send_all(const std::string& data) {
  const char* p = data.data();
  std::size_t remaining = data.size();
  while (remaining > 0) {
#ifdef _WIN32
    int n = ::send(fd_, p, static_cast<int>(remaining), 0);
#else
    ssize_t n = ::send(fd_, p, remaining, MSG_NOSIGNAL);
#endif
    if (n <= 0) return false;
    p += n;
    remaining -= static_cast<std::size_t>(n);
  }
  return true;
}

inline bool Socket::recv_all(std::string& out, std::size_t len) {
  out.assign(len, '\0');
  char* p = out.data() + 0;
  std::size_t got = 0;
  while (got < len) {
#ifdef _WIN32
    int n = ::recv(fd_, p + got, static_cast<int>(len - got), 0);
#else
    ssize_t n = ::recv(fd_, p + got, len - got, 0);
#endif
    if (n <= 0) {
      out.resize(got);
      return false;
    }
    got += static_cast<std::size_t>(n);
  }
  return true;
}

inline void Socket::close() {
  if (fd_ != kInvalidSocket) {
#ifdef _WIN32
    ::closesocket(fd_);
#else
    ::close(fd_);
#endif
    fd_ = kInvalidSocket;
  }
}

}  // namespace el
