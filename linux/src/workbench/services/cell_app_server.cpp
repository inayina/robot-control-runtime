#include "rcr/workbench/services/cell_app_server.hpp"

#include <cerrno>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rcr::workbench {
namespace {

constexpr std::size_t kMaxEngineeringClients = 8;

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

CellAppFrame make_error(std::uint16_t sequence, std::string_view message) {
  CellAppFrame reply;
  reply.type = CellAppMessage::Error;
  reply.sequence = sequence;
  reply.payload.assign(message.begin(), message.end());
  return reply;
}

CellAppFrame handle_request(CellAppHandler &handler,
                            const CellAppFrame &request) {
  CellAppFrame reply;
  reply.sequence = request.sequence;
  switch (request.type) {
  case CellAppMessage::GetStatus: {
    std::vector<std::uint8_t> payload;
    if (!encode_cell_app_status(handler.status(), payload)) {
      return make_error(request.sequence, "status encode");
    }
    reply.type = CellAppMessage::GetStatusAck;
    reply.payload = std::move(payload);
    break;
  }
  case CellAppMessage::Activate:
    reply.type = CellAppMessage::ActivateAck;
    reply.payload = encode_cell_command_reply(handler.activate());
    break;
  case CellAppMessage::SubmitOutput: {
    auto parsed = decode_cell_output_payload(request.payload);
    if (!parsed) {
      return make_error(request.sequence, parsed.error().message());
    }
    reply.type = CellAppMessage::SubmitOutputAck;
    reply.payload = encode_cell_command_reply(handler.submit_output(parsed.value()));
    break;
  }
  default:
    return make_error(request.sequence, "unknown cell message");
  }
  return reply;
}

} // namespace

CellAppServer::CellAppServer(CellAppHandler &handler) : handler_(handler) {}

CellAppServer::~CellAppServer() { close(); }

void CellAppServer::close() noexcept {
  clients_.clear();
  listen_fd_.reset();
  port_ = 0;
}

void CellAppServer::drop_client(std::size_t index) noexcept {
  if (index < clients_.size()) {
    clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(index));
  }
}

Result<void> CellAppServer::listen(const std::string &bind_address,
                                   std::uint16_t port) {
  close();
  const int raw = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
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
  if (::listen(raw, static_cast<int>(kMaxEngineeringClients)) != 0) {
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

Result<void> CellAppServer::accept_pending() {
  for (;;) {
    if (clients_.size() >= kMaxEngineeringClients) {
      return Result<void>::success();
    }
    const int client =
        ::accept4(listen_fd_.get(), nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (client < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return Result<void>::success();
      }
      if (errno == EINTR) {
        continue;
      }
      return sock_error("accept", errno);
    }
    ClientSession session;
    session.fd = OwnedFd{client};
    clients_.push_back(std::move(session));
  }
}

Result<void> CellAppServer::service_client(std::size_t index) {
  auto &session = clients_[index];
  std::uint8_t chunk[256];
  const ssize_t n = ::recv(session.fd.get(), chunk, sizeof(chunk), 0);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
    return Result<void>::success();
  }
  if (n <= 0) {
    drop_client(index);
    return Result<void>::success();
  }
  session.buffer.insert(session.buffer.end(), chunk, chunk + n);

  while (!session.buffer.empty()) {
    std::size_t consumed = 0;
    auto decoded = try_decode_cell_app_frame(session.buffer, consumed);
    if (!decoded) {
      if (decoded.error().code() == Errc::WouldBlock) {
        return Result<void>::success();
      }
      CellAppFrame error = make_error(0, decoded.error().message());
      std::vector<std::uint8_t> wire;
      if (encode_cell_app_frame(error, wire)) {
        static_cast<void>(write_all(session.fd.get(), wire));
      }
      drop_client(index);
      return Result<void>::success();
    }
    session.buffer.erase(
        session.buffer.begin(),
        session.buffer.begin() + static_cast<std::ptrdiff_t>(consumed));
    auto reply = handle_request(handler_, decoded.value());
    std::vector<std::uint8_t> wire;
    if (!encode_cell_app_frame(reply, wire)) {
      drop_client(index);
      return Error{Errc::InvalidArgument, "failed to encode cell reply"};
    }
    auto written = write_all(session.fd.get(), wire);
    if (!written) {
      drop_client(index);
      return Result<void>::success();
    }
  }
  return Result<void>::success();
}

Result<void> CellAppServer::poll(std::chrono::milliseconds timeout) {
  if (!listen_fd_.valid()) {
    return Error{Errc::NotOpen, "cell server not listening"};
  }
  std::vector<pollfd> pfds;
  pfds.reserve(clients_.size() + 1);
  pollfd listen_pfd{};
  listen_pfd.fd = listen_fd_.get();
  listen_pfd.events = POLLIN;
  pfds.push_back(listen_pfd);
  for (auto &session : clients_) {
    pollfd client_pfd{};
    client_pfd.fd = session.fd.get();
    client_pfd.events = POLLIN;
    pfds.push_back(client_pfd);
  }
  const int rc = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()),
                        static_cast<int>(timeout.count()));
  if (rc < 0 && errno == EINTR) {
    return Result<void>::success();
  }
  if (rc < 0) {
    return sock_error("poll", errno);
  }
  if (rc == 0) {
    return Result<void>::success();
  }
  if ((pfds[0].revents & POLLIN) != 0) {
    auto accepted = accept_pending();
    if (!accepted) {
      return accepted.error();
    }
  }
  // 从后往前，避免 drop_client 打乱尚未处理的下标。
  for (std::size_t i = clients_.size(); i > 0; --i) {
    const std::size_t index = i - 1;
    const auto revents = pfds[index + 1].revents;
    if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
        (revents & POLLIN) == 0) {
      drop_client(index);
      continue;
    }
    if ((revents & POLLIN) != 0) {
      auto served = service_client(index);
      if (!served) {
        return served.error();
      }
    }
  }
  return Result<void>::success();
}

} // namespace rcr::workbench
