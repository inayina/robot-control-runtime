#include "rcr_mbus/client.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <thread>

namespace rcr::mbus {

Client::Client(ClientConfig cfg) : cfg_(std::move(cfg)) {}

Client::~Client() { close(); }

void Client::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  // 换连接后半包缓冲作废，否则旧字节会污染下一事务的组帧。
  framer_.clear();
}

Result<bool> Client::connect() {
  close();

  const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return {Error::Io, false, std::string("socket: ") + std::strerror(errno)};
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(cfg_.port);
  if (::inet_pton(AF_INET, cfg_.host.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    return {Error::Io, false, "inet_pton failed"};
  }

  // 非阻塞 connect + poll：阻塞 connect 无法在用户态设“连接超时”。
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    ::close(fd);
    return {Error::Io, false, std::string("fcntl: ") + std::strerror(errno)};
  }

  const int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (rc < 0 && errno != EINPROGRESS) {
    const int e = errno;
    ::close(fd);
    return {Error::Io, false, std::string("connect: ") + std::strerror(e)};
  }
  if (rc < 0) {
    // EINPROGRESS：等可写，再查 SO_ERROR 判定真正成败。
    pollfd pfd{fd, POLLOUT, 0};
    const int pr = ::poll(&pfd, 1, static_cast<int>(cfg_.connect_timeout.count()));
    if (pr == 0) {
      ::close(fd);
      return {Error::Timeout, false, "connect timeout"};
    }
    if (pr < 0) {
      const int e = errno;
      ::close(fd);
      return {Error::Io, false, std::string("poll: ") + std::strerror(e)};
    }
    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
      ::close(fd);
      return {Error::Io, false,
              std::string("connect so_error: ") + std::strerror(so_error ? so_error : errno)};
    }
  }

  // 恢复阻塞：后续用 poll 管收发超时，避免与 O_NONBLOCK 双重语义纠缠。
  if (::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
    ::close(fd);
    return {Error::Io, false, std::string("fcntl clear O_NONBLOCK: ") + std::strerror(errno)};
  }

  fd_ = fd;
  framer_.clear();
  return {Error::Ok, true, {}};
}

Result<bool> Client::ensure_connected() {
  if (fd_ >= 0) {
    return {Error::Ok, true, {}};
  }
  return reconnect_with_backoff();
}

Result<bool> Client::reconnect_with_backoff() {
  auto delay = cfg_.reconnect_base;
  Error last = Error::Io;
  std::string last_msg = "reconnect failed";
  for (int i = 0; i < cfg_.reconnect_attempts; ++i) {
    auto r = connect();
    if (r) {
      return r;
    }
    last = r.error;
    last_msg = r.message;
    std::this_thread::sleep_for(delay);
    delay = std::min(delay * 2, cfg_.reconnect_max);
  }
  return {last, false, last_msg};
}

Result<bool> Client::send_all(std::span<const std::uint8_t> bytes) {
  // TCP 可能短写：循环直到整帧发出，每轮仍受 response_timeout 约束。
  std::size_t off = 0;
  while (off < bytes.size()) {
    pollfd pfd{fd_, POLLOUT, 0};
    const int pr = ::poll(&pfd, 1, static_cast<int>(cfg_.response_timeout.count()));
    if (pr == 0) {
      return {Error::Timeout, false, "send timeout"};
    }
    if (pr < 0) {
      return {Error::Io, false, std::string("poll send: ") + std::strerror(errno)};
    }
    const ssize_t n = ::send(fd_, bytes.data() + off, bytes.size() - off, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return {Error::Io, false, std::string("send: ") + std::strerror(errno)};
    }
    if (n == 0) {
      return {Error::Closed, false, "send returned 0"};
    }
    off += static_cast<std::size_t>(n);
  }
  return {Error::Ok, true, {}};
}

Result<std::vector<std::uint8_t>> Client::recv_adu() {
  // outstanding=1 下：反复 try_pop；NeedMore 则 poll+recv 追加半包，直到凑齐一帧。
  while (true) {
    auto popped = framer_.try_pop_adu();
    if (popped) {
      return popped;
    }
    if (popped.error != Error::NeedMore) {
      return popped;
    }

    pollfd pfd{fd_, POLLIN, 0};
    const int pr = ::poll(&pfd, 1, static_cast<int>(cfg_.response_timeout.count()));
    if (pr == 0) {
      return {Error::Timeout, {}, "response timeout"};
    }
    if (pr < 0) {
      return {Error::Io, {}, std::string("poll recv: ") + std::strerror(errno)};
    }
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      return {Error::Closed, {}, "peer closed or error"};
    }

    std::uint8_t buf[512];
    const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return {Error::Io, {}, std::string("recv: ") + std::strerror(errno)};
    }
    if (n == 0) {
      return {Error::Closed, {}, "peer EOF"};
    }
    framer_.append(std::span<const std::uint8_t>(buf, static_cast<std::size_t>(n)));
  }
}

Result<Adu> Client::transact_once(std::vector<std::uint8_t> pdu, std::uint16_t tid) {
  // 一笔完整事务：拼 MBAP+PDU → 发送 → 收一帧 → 校验 TransID/UnitID。
  Adu req;
  req.mbap.transaction_id = tid;
  req.mbap.protocol_id = kProtocolId;
  req.mbap.unit_id = cfg_.unit_id;
  req.pdu = std::move(pdu);

  auto encoded = encode_adu(req);
  if (!encoded) {
    return {encoded.error, {}, encoded.message};
  }
  auto sent = send_all(encoded.value);
  if (!sent) {
    return {sent.error, {}, sent.message};
  }
  auto frame = recv_adu();
  if (!frame) {
    return {frame.error, {}, frame.message};
  }
  auto adu = decode_adu(frame.value);
  if (!adu) {
    return adu;
  }
  if (adu.value.mbap.transaction_id != tid) {
    return {Error::TransactionMismatch, {}, "response transaction id mismatch"};
  }
  if (adu.value.mbap.unit_id != cfg_.unit_id) {
    return {Error::UnexpectedPdu, {}, "unit id mismatch"};
  }
  return adu;
}

Result<Adu> Client::transact_raw_expect_tid(std::vector<std::uint8_t> pdu, std::uint16_t tid) {
  auto ok = ensure_connected();
  if (!ok) {
    return {ok.error, {}, ok.message};
  }
  return transact_once(std::move(pdu), tid);
}

Result<Adu> Client::transact_raw(std::vector<std::uint8_t> pdu) {
  auto ok = ensure_connected();
  if (!ok) {
    return {ok.error, {}, ok.message};
  }
  const std::vector<std::uint8_t> pdu_copy = pdu;
  const std::uint16_t tid = next_tid_++;
  if (next_tid_ == 0) {
    next_tid_ = 1;  // 跳过 0，避免与“未置位”混淆
  }
  auto r = transact_once(std::move(pdu), tid);
  // 仅对连接级失败重试：Exception/Timeout/Mismatch 不自动重放（避免双写歧义放大）。
  if (!r && (r.error == Error::Closed || r.error == Error::Io)) {
    close();
    auto again = reconnect_with_backoff();
    if (!again) {
      return {again.error, {}, again.message};
    }
    const std::uint16_t tid2 = next_tid_++;
    if (next_tid_ == 0) {
      next_tid_ = 1;
    }
    return transact_once(pdu_copy, tid2);
  }
  return r;
}

Result<std::vector<std::uint16_t>> Client::read_holding(std::uint16_t address,
                                                       std::uint16_t quantity) {
  auto pdu = encode_read_holding_request(address, quantity);
  if (!pdu) {
    return {pdu.error, {}, pdu.message};
  }
  auto adu = transact_raw(std::move(pdu.value));
  if (!adu) {
    return {adu.error, {}, adu.message};
  }
  // exception 路径：PDU[0] 最高位为 1 → 映射为 Error::ExceptionResponse，不假装读成功。
  if (!adu.value.pdu.empty() && (adu.value.pdu[0] & 0x80u) != 0) {
    auto ex = decode_exception_response(adu.value.pdu);
    return {Error::ExceptionResponse, {},
            ex ? ("exception code=" + std::to_string(ex.value.code)) : "exception"};
  }
  auto decoded = decode_read_holding_response(adu.value.pdu);
  if (!decoded) {
    return {decoded.error, {}, decoded.message};
  }
  return {Error::Ok, std::move(decoded.value.values), {}};
}

Result<WriteSingleResponse> Client::write_single(std::uint16_t address, std::uint16_t value) {
  auto pdu = encode_write_single_request(address, value);
  if (!pdu) {
    return {pdu.error, {}, pdu.message};
  }
  auto adu = transact_raw(std::move(pdu.value));
  if (!adu) {
    return {adu.error, {}, adu.message};
  }
  if (!adu.value.pdu.empty() && (adu.value.pdu[0] & 0x80u) != 0) {
    auto ex = decode_exception_response(adu.value.pdu);
    return {Error::ExceptionResponse, {},
            ex ? ("exception code=" + std::to_string(ex.value.code)) : "exception"};
  }
  return decode_write_single_response(adu.value.pdu);
}

Result<WriteMultipleResponse> Client::write_multiple(std::uint16_t address,
                                                     std::span<const std::uint16_t> values) {
  auto pdu = encode_write_multiple_request(address, values);
  if (!pdu) {
    return {pdu.error, {}, pdu.message};
  }
  auto adu = transact_raw(std::move(pdu.value));
  if (!adu) {
    return {adu.error, {}, adu.message};
  }
  if (!adu.value.pdu.empty() && (adu.value.pdu[0] & 0x80u) != 0) {
    auto ex = decode_exception_response(adu.value.pdu);
    return {Error::ExceptionResponse, {},
            ex ? ("exception code=" + std::to_string(ex.value.code)) : "exception"};
  }
  return decode_write_multiple_response(adu.value.pdu);
}

}  // namespace rcr::mbus
