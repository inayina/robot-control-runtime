#include "rcr/workbench/services/modbus_agent_client.hpp"

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
    const ssize_t n = ::send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
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
      return Error{Errc::Timeout, "agent reply timeout"};
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
      return Error{Errc::IoError, "agent disconnected"};
    }
    buffer.insert(buffer.end(), chunk, chunk + n);
    std::size_t consumed = 0;
    auto decoded = try_decode_modbus_agent_frame(buffer, consumed);
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
  return Error{Errc::Timeout, "agent reply timeout"};
}

} // namespace

ModbusAgentClient::~ModbusAgentClient() { disconnect(); }

void ModbusAgentClient::disconnect() noexcept {
  if (!fd_.valid()) {
    return;
  }
  // shutdown 可从 UI 线程打断 worker 里的 poll/recv，避免窗口关闭时 join 卡住。
  static_cast<void>(::shutdown(fd_.get(), SHUT_RDWR));
  fd_.reset();
}

Result<void> ModbusAgentClient::connect(const std::string &host, std::uint16_t port,
                                        std::chrono::milliseconds timeout) {
  disconnect();
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *info = nullptr;
  const int rc = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &info);
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
      return Error{Errc::Timeout, "agent connect timeout"};
    }
    if (poll_rc < 0) {
      return sock_error("connect poll", errno);
    }
    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if (::getsockopt(raw, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0 || so_error != 0) {
      return sock_error("connect", so_error != 0 ? so_error : errno);
    }
  }
  fd_ = std::move(owned);
  return Result<void>::success();
}

Result<ModbusIoSnapshot>
ModbusAgentClient::exchange(ModbusAgentMessage request_type,
                            ModbusAgentMessage ack_type,
                            std::vector<std::uint8_t> payload,
                            std::chrono::milliseconds timeout) {
  if (!fd_.valid()) {
    return Error{Errc::NotOpen, "agent client not connected"};
  }
  ModbusAgentFrame request;
  request.type = request_type;
  request.sequence = next_sequence_++;
  request.payload = std::move(payload);
  std::vector<std::uint8_t> wire;
  if (!encode_modbus_agent_frame(request, wire)) {
    return Error{Errc::InvalidArgument, "failed to encode agent request"};
  }
  auto sent = write_all(fd_.get(), wire);
  if (!sent) {
    return sent.error();
  }
  auto raw = read_frame(fd_.get(), timeout);
  if (!raw) {
    return raw.error();
  }
  std::size_t consumed = 0;
  auto frame = try_decode_modbus_agent_frame(raw.value(), consumed);
  if (!frame) {
    return frame.error();
  }
  if (frame.value().type == ModbusAgentMessage::Error) {
    return Error{Errc::Rejected, "agent returned ERROR"};
  }
  if (frame.value().type != ack_type) {
    return Error{Errc::Rejected, "unexpected agent message"};
  }
  return decode_probe_ack_payload(frame.value().payload);
}

Result<ModbusIoSnapshot>
ModbusAgentClient::probe(std::chrono::milliseconds timeout) {
  return exchange(ModbusAgentMessage::Probe, ModbusAgentMessage::ProbeAck, {},
                  timeout);
}

Result<ModbusIoSnapshot>
ModbusAgentClient::read_inputs(std::chrono::milliseconds timeout) {
  return exchange(ModbusAgentMessage::ReadDi, ModbusAgentMessage::ReadDiAck, {},
                  timeout);
}

Result<ModbusIoSnapshot>
ModbusAgentClient::write_output(std::uint8_t channel, bool active,
                                std::chrono::milliseconds timeout) {
  return exchange(ModbusAgentMessage::WriteDo, ModbusAgentMessage::WriteDoAck,
                  encode_write_do_payload(channel, active), timeout);
}

Result<ModbusIoSnapshot>
ModbusAgentClient::write_all_outputs_off(std::chrono::milliseconds timeout) {
  return exchange(ModbusAgentMessage::AllOff, ModbusAgentMessage::AllOffAck, {},
                  timeout);
}

} // namespace rcr::workbench
