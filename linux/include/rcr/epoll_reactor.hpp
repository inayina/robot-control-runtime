#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。

#include "rcr/result.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace rcr {

struct ReadyFd {
  /// 原样返回注册时存入 epoll_event.data.fd 的非 owning fd。
  int fd{-1};
  /// EPOLLIN/EPOLLOUT/EPOLLERR/EPOLLHUP 等位集合；调用方必须逐位解释，不能只看 EPOLLIN。
  std::uint32_t events{0};
};

/**
 * Runtime I/O 线程使用的 Linux epoll 最小封装。
 *
 * 本类拥有 epoll fd，但不拥有被监视的 CAN、eventfd 或 signal fd。调用方必须在
 * 关闭业务 fd 前先 remove，且同一 EpollReactor 只应由一个等待线程调用 wait。
 * add/modify/remove 只是维护内核 interest list，不会读取业务 fd，也不会执行 callback。
 * wait 返回 readiness 快照；业务 fd 应设为非阻塞并在读写时处理 EAGAIN。
 *
 * V1 默认由调用方注册 level-triggered 事件。若未来使用 EPOLLET，调用方还必须建立
 * “持续读到 EAGAIN”的新合同，不能只改一个 flag。
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
  /// 把非 owning fd 加入内核 interest list；重复添加由 epoll_ctl 返回错误。
  Result<void> add(int fd, std::uint32_t events);
  /// 修改已注册 fd 的关注事件；不会改变 fd 本身的 blocking flags。
  Result<void> modify(int fd, std::uint32_t events);
  /// 仅取消监视，不 close 业务 fd；关闭责任仍在 fd 拥有者。
  Result<void> remove(int fd);
  /// timeout=-1 无限等待，0 立即返回，正值为毫秒上限；max_events 控制单次返回容量。
  Result<std::vector<ReadyFd>> wait(std::chrono::milliseconds timeout,
                                    std::size_t max_events = 16);

 private:
  Result<void> control(int operation, int fd, std::uint32_t events);
  // epoll_fd_ 是本对象唯一拥有的内核 epoll 实例；-1 表示构造失败或已被移动。
  int epoll_fd_{-1};
};

}  // namespace rcr
