#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。

#include "rcr/result.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace rcr {

struct ReadyFd {
  int fd{-1};
  std::uint32_t events{0};
};

/**
 * Runtime I/O 线程使用的 Linux epoll 最小封装。
 *
 * 本类拥有 epoll fd，但不拥有被监视的 CAN、eventfd 或 signal fd。调用方必须在
 * 关闭业务 fd 前先 remove，且同一 EpollReactor 只应由一个等待线程调用 wait。
 */
class EpollReactor {
 public:
  EpollReactor();
  ~EpollReactor();

  EpollReactor(const EpollReactor&) = delete;
  EpollReactor& operator=(const EpollReactor&) = delete;
  EpollReactor(EpollReactor&& other) noexcept;
  EpollReactor& operator=(EpollReactor&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  Result<void> add(int fd, std::uint32_t events);
  Result<void> modify(int fd, std::uint32_t events);
  Result<void> remove(int fd);
  Result<std::vector<ReadyFd>> wait(std::chrono::milliseconds timeout,
                                    std::size_t max_events = 16);

 private:
  Result<void> control(int operation, int fd, std::uint32_t events);
  int epoll_fd_{-1};
};

}  // namespace rcr
