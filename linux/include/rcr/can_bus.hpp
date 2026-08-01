#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。

#include "rcr/result.hpp"
#include "rcr/types.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rcr {

/**
 * CAN 收发的最小测试接缝：实机使用 SocketCan，单元测试使用 FakeCanBus。
 *
 * 该接口只服务第一版 CAN，不是通用 Transport 抽象；以后引入 Modbus 或
 * EtherCAT 时应按实际语义独立设计，不能强行套入此接口。
 */
class ICanBus {
 public:
  virtual ~ICanBus() = default;

  [[nodiscard]] virtual bool is_open() const noexcept = 0;
  [[nodiscard]] virtual std::string_view interface_name() const noexcept = 0;

  virtual Result<void> open() = 0;
  virtual void close() = 0;

  virtual Result<void> send(const CanFrame& frame) = 0;
  virtual Result<CanFrame> receive(std::chrono::milliseconds timeout) = 0;
};

/**
 * Linux SocketCAN 后端（PF_CAN / SOCK_RAW / CAN_RAW）。
 * 可连接软件 vcan0，也可连接 Orange Pi 上 MCP2515 注册的 can0。对象独占 fd，
 * 移动后原对象失去 fd 所有权；receive 的非负 timeout 使用 select 限时等待。
 */
class SocketCan final : public ICanBus {
 public:
  explicit SocketCan(std::string interface_name);
  ~SocketCan() override;

  SocketCan(const SocketCan&) = delete;
  SocketCan& operator=(const SocketCan&) = delete;

  SocketCan(SocketCan&& other) noexcept;
  SocketCan& operator=(SocketCan&& other) noexcept;

  [[nodiscard]] bool is_open() const noexcept override;
  [[nodiscard]] std::string_view interface_name() const noexcept override;

  Result<void> open() override;
  void close() override;

  Result<void> send(const CanFrame& frame) override;
  Result<CanFrame> receive(std::chrono::milliseconds timeout) override;

  /// 打开后切换非阻塞模式；默认由阻塞 fd 配合 select 超时。
  Result<void> set_nonblocking(bool enabled);

  /**
   * 非 owning 的内核 fd，供 EpollReactor 注册。
   * 未打开、已关闭或移动后的空对象返回 -1；调用方不得 close 该句柄。
   * 不放入 ICanBus：FakeCanBus 没有 native fd，通用 Transport 也不是本阶段目标。
   */
  [[nodiscard]] int native_handle() const noexcept;

 private:
  std::string ifname_;
  int fd_{-1};
};

/**
 * 隔离单元测试使用的内存 CAN 队列，不依赖内核或 vcan。
 * send 的帧在同一实例中排队等待 receive，便于验证接口语义；它不模拟总线仲裁、
 * 位时序、错误帧、并发安全或真实通信延迟，不能替代 SocketCAN 和实物验收。
 */
class FakeCanBus final : public ICanBus {
 public:
  explicit FakeCanBus(std::string interface_name = "fake0");

  [[nodiscard]] bool is_open() const noexcept override;
  [[nodiscard]] std::string_view interface_name() const noexcept override;

  Result<void> open() override;
  void close() override;

  Result<void> send(const CanFrame& frame) override;
  Result<CanFrame> receive(std::chrono::milliseconds timeout) override;

  [[nodiscard]] std::size_t queued() const noexcept;
  void clear();

 private:
  std::string ifname_;
  bool open_{false};
  std::vector<CanFrame> rx_queue_{};
};

}  // namespace rcr
