#include "rcr_mbus/server.hpp"

#include "rcr_mbus/codec.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <thread>
#include <vector>

namespace rcr::mbus {

RefServer::RefServer(ServerConfig cfg) : cfg_(std::move(cfg)), map_(cfg_.holding_count) {}

RefServer::~RefServer() { stop(); }

Result<bool> RefServer::start() {
  if (running_.load()) {
    return {Error::Busy, false, "already running"};
  }

  const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return {Error::Io, false, std::string("socket: ") + std::strerror(errno)};
  }
  int yes = 1;
  (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(cfg_.port);
  if (::inet_pton(AF_INET, cfg_.bind_host.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    return {Error::Io, false, "inet_pton failed"};
  }
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    const int e = errno;
    ::close(fd);
    return {Error::Io, false, std::string("bind: ") + std::strerror(e)};
  }
  if (::listen(fd, 16) < 0) {
    const int e = errno;
    ::close(fd);
    return {Error::Io, false, std::string("listen: ") + std::strerror(e)};
  }

  // port=0 时由内核分配；测试用动态端口避免 1502 冲突。
  sockaddr_in bound{};
  socklen_t blen = sizeof(bound);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &blen) == 0) {
    bound_port_ = ntohs(bound.sin_port);
  } else {
    bound_port_ = cfg_.port;
  }

  listen_fd_ = fd;
  running_.store(true);
  thread_ = std::thread([this] { thread_main(); });
  return {Error::Ok, true, {}};
}

void RefServer::stop() {
  running_.store(false);
  if (listen_fd_ >= 0) {
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

void RefServer::thread_main() {
  // 单线程：accept 一个 → 服务到断开 → 再 accept。教学简单；非高并发服务器。
  while (running_.load()) {
    pollfd pfd{listen_fd_, POLLIN, 0};
    const int pr = ::poll(&pfd, 1, 100);
    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (pr == 0) {
      continue;
    }
    const int client = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
      if (!running_.load()) {
        break;
      }
      continue;
    }
    serve_client(client);
    ::close(client);
  }
}

void RefServer::serve_client(int client_fd) {
  StreamFramer framer;
  while (running_.load()) {
    pollfd pfd{client_fd, POLLIN, 0};
    const int pr = ::poll(&pfd, 1, 200);
    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }
      return;
    }
    if (pr == 0) {
      continue;
    }
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      return;
    }

    std::uint8_t buf[512];
    const ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
    if (n <= 0) {
      return;
    }
    framer.append(std::span<const std::uint8_t>(buf, static_cast<std::size_t>(n)));

    // 粘包：一次 recv 可能含多请求；逐帧弹出并各回一帧（顺序服务 → outstanding≈1）。
    while (true) {
      auto frame = framer.try_pop_adu();
      if (frame.error == Error::NeedMore) {
        break;
      }
      if (!frame) {
        // 非法 length/协议：关闭连接，避免在坏流上死循环。
        return;
      }
      auto adu = decode_adu(frame.value);
      if (!adu) {
        return;
      }

      if (cfg_.response_delay.count() > 0) {
        std::this_thread::sleep_for(cfg_.response_delay);
      }

      // 应用层：HoldingMap 返回正常或 exception PDU；MBAP 回显同一 TransID/Unit。
      auto resp_pdu = map_.handle_pdu(adu.value.pdu);
      if (!resp_pdu) {
        return;
      }
      Adu resp;
      resp.mbap = adu.value.mbap;
      resp.mbap.protocol_id = kProtocolId;
      resp.pdu = std::move(resp_pdu.value);
      auto encoded = encode_adu(resp);
      if (!encoded) {
        return;
      }
      std::size_t off = 0;
      while (off < encoded.value.size()) {
        const ssize_t sn =
            ::send(client_fd, encoded.value.data() + off, encoded.value.size() - off, MSG_NOSIGNAL);
        if (sn <= 0) {
          return;
        }
        off += static_cast<std::size_t>(sn);
      }
    }
  }
}

}  // namespace rcr::mbus
