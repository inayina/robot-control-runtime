#include "rcr/workbench/services/cell_app_client.hpp"

#include <cerrno>
#include <cstring>
#include <span>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rcr::workbench {
namespace {

Error sock_error(std::string_view prefix, int err) {
  return Error{Errc::IoError, std::string(prefix) + ": " + std::strerror(err)};
}

Result<void> write_all(int fd, std::span<const std::uint8_t> bytes) {
  std::size_t sent = 0;
  while (sent < bytes.size()) {
    const ssize_t n =
        ::send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      return sock_error("send", errno);
    }
    sent += static_cast<std::size_t>(n);
  }
  return Result<void>::success();
}

Result<std::vector<std::uint8_t>>
read_frame(int fd, std::chrono::milliseconds timeout) {
  std::vector<std::uint8_t> buffer;
  buffer.reserve(64);
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (left.count() <= 0) {
      break;
    }
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    const int rc = ::poll(&pfd, 1, static_cast<int>(left.count()));
    if (rc < 0 && errno == EINTR) {
      continue;
    }
    if (rc < 0) {
      return sock_error("poll", errno);
    }
    if (rc == 0) {
      return Error{Errc::Timeout, "cell reply timeout"};
    }
    std::uint8_t chunk[256];
    const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      return sock_error("recv", errno);
    }
    if (n == 0) {
      return Error{Errc::IoError, "cell app disconnected"};
    }
    buffer.insert(buffer.end(), chunk, chunk + n);
    std::size_t consumed = 0;
    auto decoded = try_decode_cell_app_frame(buffer, consumed);
    if (decoded) {
      return std::vector<std::uint8_t>(
          buffer.begin(),
          buffer.begin() + static_cast<std::ptrdiff_t>(consumed));
    }
    if (decoded.error().code() == Errc::WouldBlock) {
      continue;
    }
    return decoded.error();
  }
  return Error{Errc::Timeout, "cell reply timeout"};
}

} // namespace

CellAppClient::~CellAppClient() { disconnect(); }

void CellAppClient::disconnect() noexcept {
  if (!fd_.valid()) {
    return;
  }
  // shutdown 可从 UI 线程打断阻塞 poll/recv，避免窗口关闭时 join 卡住。
  static_cast<void>(::shutdown(fd_.get(), SHUT_RDWR));
  fd_.reset();
}

Result<void> CellAppClient::connect(const std::string &host, std::uint16_t port,
                                    std::chrono::milliseconds timeout) {
  host_ = host;
  port_ = port;
  disconnect();
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *info = nullptr;
  const int rc = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(),
                               &hints, &info);
  if (rc != 0 || info == nullptr) {
    return Error{Errc::InvalidArgument,
                 std::string("getaddrinfo: ") + ::gai_strerror(rc)};
  }
  const int raw = ::socket(info->ai_family, info->ai_socktype | SOCK_CLOEXEC,
                           info->ai_protocol);
  if (raw < 0) {
    const int err = errno;
    ::freeaddrinfo(info);
    return sock_error("socket", err);
  }
  OwnedFd owned{raw};
  const int flags = ::fcntl(raw, F_GETFL, 0);
  if (flags < 0 || ::fcntl(raw, F_SETFL, flags | O_NONBLOCK) != 0) {
    const int err = errno;
    ::freeaddrinfo(info);
    return sock_error("fcntl", err);
  }
  int connected = ::connect(raw, info->ai_addr, info->ai_addrlen);
  ::freeaddrinfo(info);
  if (connected != 0 && errno != EINPROGRESS) {
    return sock_error("connect", errno);
  }
  if (connected != 0) {
    pollfd pfd{};
    pfd.fd = raw;
    pfd.events = POLLOUT;
    const int poll_rc = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
    if (poll_rc == 0) {
      return Error{Errc::Timeout, "cell connect timeout"};
    }
    if (poll_rc < 0) {
      return sock_error("connect poll", errno);
    }
    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if (::getsockopt(raw, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0 ||
        so_error != 0) {
      return sock_error("connect", so_error != 0 ? so_error : errno);
    }
  }
  fd_ = std::move(owned);
  return Result<void>::success();
}

Result<void> CellAppClient::reconnect(std::chrono::milliseconds timeout) {
  if (host_.empty() || port_ == 0) {
    return Error{Errc::NotOpen, "cell client has no peer"};
  }
  return connect(host_, port_, timeout);
}

Result<CellAppFrame>
CellAppClient::exchange(CellAppMessage request_type, CellAppMessage ack_type,
                        std::vector<std::uint8_t> payload,
                        std::chrono::milliseconds timeout) {
  if (!fd_.valid()) {
    return Error{Errc::NotOpen, "cell client not connected"};
  }
  CellAppFrame request;
  request.type = request_type;
  request.sequence = next_sequence_++;
  request.payload = std::move(payload);
  std::vector<std::uint8_t> wire;
  if (!encode_cell_app_frame(request, wire)) {
    return Error{Errc::InvalidArgument, "failed to encode cell request"};
  }
  auto sent = write_all(fd_.get(), wire);
  if (!sent) {
    disconnect();
    return sent.error();
  }
  auto raw = read_frame(fd_.get(), timeout);
  if (!raw) {
    // 超时后 ACK 可能仍在路上；留下半帧会让下一次 Activate 读成 GetStatusAck。
    disconnect();
    return raw.error();
  }
  std::size_t consumed = 0;
  auto frame = try_decode_cell_app_frame(raw.value(), consumed);
  if (!frame) {
    disconnect();
    return frame.error();
  }
  if (frame.value().type == CellAppMessage::Error) {
    return Error{Errc::Rejected, "cell app returned ERROR"};
  }
  if (frame.value().type != ack_type) {
    disconnect();
    return Error{Errc::Rejected, "unexpected cell message"};
  }
  return frame;
}

Result<CellAppStatus>
CellAppClient::get_status(std::chrono::milliseconds timeout) {
  auto frame =
      exchange(CellAppMessage::GetStatus, CellAppMessage::GetStatusAck, {},
               timeout);
  if (!frame) {
    return frame.error();
  }
  return decode_cell_app_status(frame.value().payload);
}

Result<CellAppStatus>
CellAppClient::probe_cell_io(std::chrono::milliseconds timeout) {
  auto frame = exchange(CellAppMessage::ProbeCellIo,
                        CellAppMessage::ProbeCellIoAck, {}, timeout);
  if (!frame) {
    return frame.error();
  }
  return decode_cell_app_status(frame.value().payload);
}

Result<CommandReply>
CellAppClient::activate(std::chrono::milliseconds timeout) {
  auto frame = exchange(CellAppMessage::Activate, CellAppMessage::ActivateAck,
                        {}, timeout);
  if (!frame) {
    return frame.error();
  }
  return decode_cell_command_reply(frame.value().payload);
}

Result<CommandReply>
CellAppClient::submit_output(const DigitalOutputRequest &request,
                             std::chrono::milliseconds timeout) {
  auto frame =
      exchange(CellAppMessage::SubmitOutput, CellAppMessage::SubmitOutputAck,
               encode_cell_output_payload(request), timeout);
  if (!frame) {
    return frame.error();
  }
  return decode_cell_command_reply(frame.value().payload);
}

} // namespace rcr::workbench
