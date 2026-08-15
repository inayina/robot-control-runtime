#include "rcr/workbench/services/posix_serial_port.hpp"

#include <chrono>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace rcr::workbench {
namespace {

Error io_error(std::string_view prefix, int err) {
  return Error{Errc::IoError, std::string(prefix) + ": " + std::strerror(err)};
}

speed_t posix_baud(std::uint32_t baud) {
  switch (baud) {
  case 9600:
    return B9600;
  case 19200:
    return B19200;
  case 38400:
    return B38400;
  case 115200:
    return B115200;
  default:
    return static_cast<speed_t>(0);
  }
}

} // namespace

Result<void> PosixSerialPort::open(const PosixSerialConfig &config) {
  close();
  const speed_t speed = posix_baud(config.baud_rate);
  if (speed == 0) {
    return Error{Errc::InvalidArgument, "unsupported serial baud"};
  }
  if (config.parity != 'N' && config.parity != 'E' && config.parity != 'O') {
    return Error{Errc::InvalidArgument, "unsupported serial parity"};
  }

  const int fd = ::open(config.device.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (fd < 0) {
    return io_error("open serial", errno);
  }
  OwnedFd owned{fd};

  termios attrs{};
  if (::tcgetattr(fd, &attrs) != 0) {
    return io_error("tcgetattr", errno);
  }
  ::cfmakeraw(&attrs);
  attrs.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD | CS8);
  attrs.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
  if (config.parity == 'E') {
    attrs.c_cflag |= static_cast<tcflag_t>(PARENB);
    attrs.c_cflag &= static_cast<tcflag_t>(~PARODD);
  } else if (config.parity == 'O') {
    attrs.c_cflag |= static_cast<tcflag_t>(PARENB | PARODD);
  } else {
    attrs.c_cflag &= static_cast<tcflag_t>(~(PARENB | PARODD));
  }
  if (::cfsetispeed(&attrs, speed) != 0 || ::cfsetospeed(&attrs, speed) != 0) {
    return io_error("cfsetspeed", errno);
  }
  attrs.c_cc[VMIN] = 0;
  attrs.c_cc[VTIME] = 0;
  if (::tcsetattr(fd, TCSANOW, &attrs) != 0) {
    return io_error("tcsetattr", errno);
  }
  static_cast<void>(::tcflush(fd, TCIOFLUSH));

  fd_ = std::move(owned);
  device_ = config.device;
  return Result<void>::success();
}

void PosixSerialPort::close() noexcept {
  fd_.reset();
  device_.clear();
}

Result<std::vector<std::uint8_t>>
PosixSerialPort::transact(std::span<const std::uint8_t> request,
                          std::chrono::milliseconds timeout) {
  if (!fd_.valid()) {
    return Error{Errc::NotOpen, "serial port not open"};
  }
  if (request.empty()) {
    return Error{Errc::InvalidArgument, "empty RTU request"};
  }
  if (timeout.count() <= 0) {
    return Error{Errc::InvalidArgument, "serial timeout must be positive"};
  }

  static_cast<void>(::tcflush(fd_.get(), TCIFLUSH));
  std::size_t written = 0;
  while (written < request.size()) {
    const ssize_t n = ::write(fd_.get(), request.data() + written,
                              request.size() - written);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      return io_error("serial write", errno);
    }
    written += static_cast<std::size_t>(n);
  }
  if (::tcdrain(fd_.get()) != 0) {
    return io_error("tcdrain", errno);
  }

  // 3.5 字符间隔 + SP3485 方向翻转余量；9600 下约 4 ms，这里留 8 ms。
  ::usleep(8000);

  std::vector<std::uint8_t> reply;
  reply.reserve(32);
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (left.count() <= 0) {
      break;
    }
    pollfd pfd{};
    pfd.fd = fd_.get();
    pfd.events = POLLIN;
    const int rc = ::poll(&pfd, 1, static_cast<int>(left.count()));
    if (rc < 0 && errno == EINTR) {
      continue;
    }
    if (rc < 0) {
      return io_error("serial poll", errno);
    }
    if (rc == 0) {
      break;
    }
    std::uint8_t buf[64];
    const ssize_t n = ::read(fd_.get(), buf, sizeof(buf));
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      return io_error("serial read", errno);
    }
    if (n == 0) {
      return Error{Errc::IoError, "serial hung up"};
    }
    reply.insert(reply.end(), buf, buf + n);

    // 已知读线圈/离散输入应答：slave + func + bytecount + data + CRC。
    // 不用过短的字符间隔去赌最后一字节，避免把合法 FC02 裁成 CRC mismatch。
    if (reply.size() >= 5) {
      const std::uint8_t function = reply[1];
      if ((function & 0x80u) != 0) {
        if (reply.size() >= 5) {
          return reply;
        }
      } else if (function == 0x01 || function == 0x02) {
        const std::size_t expected =
            static_cast<std::size_t>(5) + static_cast<std::size_t>(reply[2]);
        if (reply.size() >= expected) {
          reply.resize(expected);
          return reply;
        }
      } else if (function == 0x05) {
        // FC05 应答回显 8 字节请求，按长度收齐，不用字符静默窗口。
        if (reply.size() >= 8) {
          reply.resize(8);
          return reply;
        }
      } else if (reply.size() >= 8) {
        return reply;
      }
    }
  }
  if (reply.empty()) {
    return Error{Errc::Timeout, "serial RTU timeout"};
  }
  return reply;
}

} // namespace rcr::workbench
