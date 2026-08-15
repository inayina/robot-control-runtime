#include "rcr/workbench/services/modbus_agent_server.hpp"

#include "rcr/workbench/application/modbus_agent_protocol.hpp"

#include <cerrno>
#include <cstring>
#include <vector>

#include <arpa/inet.h>
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

Result<ModbusAgentFrame> read_request(int fd, std::chrono::milliseconds timeout) {
  std::vector<std::uint8_t> buffer;
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
    if (rc <= 0) {
      return rc == 0 ? Error{Errc::Timeout, "agent request timeout"}
                     : sock_error("poll", errno);
    }
    std::uint8_t chunk[256];
    const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      return Error{Errc::IoError, "client disconnected"};
    }
    buffer.insert(buffer.end(), chunk, chunk + n);
    std::size_t consumed = 0;
    auto decoded = try_decode_modbus_agent_frame(buffer, consumed);
    if (decoded) {
      return decoded;
    }
    if (decoded.error().code() == Errc::WouldBlock) {
      continue;
    }
    return decoded.error();
  }
  return Error{Errc::Timeout, "agent request timeout"};
}

ModbusAgentFrame handle_request(PhysicalModbusIoService &service,
                                const ModbusAgentFrame &request) {
  ModbusAgentFrame reply;
  reply.sequence = request.sequence;
  switch (request.type) {
  case ModbusAgentMessage::Probe:
    static_cast<void>(service.probe());
    reply.type = ModbusAgentMessage::ProbeAck;
    reply.payload = encode_probe_ack_payload(service.snapshot());
    break;
  case ModbusAgentMessage::ReadDi:
    static_cast<void>(service.read_inputs());
    reply.type = ModbusAgentMessage::ReadDiAck;
    reply.payload = encode_probe_ack_payload(service.snapshot());
    break;
  case ModbusAgentMessage::WriteDo: {
    auto parsed = decode_write_do_payload(request.payload);
    if (!parsed) {
      reply.type = ModbusAgentMessage::Error;
      break;
    }
    static_cast<void>(
        service.write_output(parsed.value().first, parsed.value().second));
    reply.type = ModbusAgentMessage::WriteDoAck;
    reply.payload = encode_probe_ack_payload(service.snapshot());
    break;
  }
  case ModbusAgentMessage::AllOff:
    static_cast<void>(service.write_all_outputs_off());
    reply.type = ModbusAgentMessage::AllOffAck;
    reply.payload = encode_probe_ack_payload(service.snapshot());
    break;
  default:
    reply.type = ModbusAgentMessage::Error;
    break;
  }
  return reply;
}

} // namespace

ModbusAgentServer::ModbusAgentServer(PhysicalModbusIoService &service)
    : service_(service) {}

ModbusAgentServer::~ModbusAgentServer() { close(); }

void ModbusAgentServer::close() noexcept {
  listen_fd_.reset();
  port_ = 0;
}

Result<void> ModbusAgentServer::listen(const std::string &bind_address,
                                       std::uint16_t port) {
  close();
  const int raw = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (raw < 0) {
    return sock_error("socket", errno);
  }
  OwnedFd owned{raw};
  int yes = 1;
  static_cast<void>(
      ::setsockopt(raw, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (::inet_pton(AF_INET, bind_address.c_str(), &addr.sin_addr) != 1) {
    return Error{Errc::InvalidArgument, "invalid bind address"};
  }
  if (::bind(raw, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    return sock_error("bind", errno);
  }
  if (::listen(raw, 1) != 0) {
    return sock_error("listen", errno);
  }
  sockaddr_in bound{};
  socklen_t bound_len = sizeof(bound);
  if (::getsockname(raw, reinterpret_cast<sockaddr *>(&bound), &bound_len) != 0) {
    return sock_error("getsockname", errno);
  }
  listen_fd_ = std::move(owned);
  port_ = ntohs(bound.sin_port);
  return Result<void>::success();
}

Result<void>
ModbusAgentServer::serve_one(std::chrono::milliseconds accept_timeout) {
  if (!listen_fd_.valid()) {
    return Error{Errc::NotOpen, "agent server not listening"};
  }
  pollfd pfd{};
  pfd.fd = listen_fd_.get();
  pfd.events = POLLIN;
  const int rc = ::poll(&pfd, 1, static_cast<int>(accept_timeout.count()));
  if (rc == 0) {
    return Error{Errc::Timeout, "accept timeout"};
  }
  if (rc < 0) {
    return sock_error("accept poll", errno);
  }
  const int client = ::accept4(listen_fd_.get(), nullptr, nullptr, SOCK_CLOEXEC);
  if (client < 0) {
    return sock_error("accept", errno);
  }
  OwnedFd client_fd{client};

  // 同一 TCP 连接上处理 Probe / 轮询 / 写线圈，直到客户端断开。
  // 空闲 5 s 结束会话，避免测试线程在客户端已关后一直堵在 accept 循环里。
  bool served = false;
  for (;;) {
    const auto idle = served ? std::chrono::milliseconds{5000}
                             : std::chrono::milliseconds{1000};
    auto request = read_request(client, idle);
    if (!request) {
      if (served && (request.error().code() == Errc::Timeout ||
                     request.error().code() == Errc::IoError)) {
        return Result<void>::success();
      }
      return request.error();
    }
    auto reply = handle_request(service_, request.value());
    std::vector<std::uint8_t> wire;
    if (!encode_modbus_agent_frame(reply, wire)) {
      return Error{Errc::InvalidArgument, "failed to encode agent reply"};
    }
    auto written = write_all(client, wire);
    if (!written) {
      return written.error();
    }
    served = true;
  }
}

} // namespace rcr::workbench
