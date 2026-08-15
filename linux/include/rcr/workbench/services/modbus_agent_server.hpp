#pragma once

// Orange Pi 侧单连接 agent：accept 一次，在同一 TCP 会话上处理 Probe / 读 DI /
// 写 DO，把 RTU 事务留在本进程。

#include "rcr/owned_fd.hpp"
#include "rcr/result.hpp"
#include "rcr/workbench/services/physical_modbus_io_service.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace rcr::workbench {

class ModbusAgentServer {
public:
  explicit ModbusAgentServer(PhysicalModbusIoService &service);
  ~ModbusAgentServer();

  [[nodiscard]] Result<void> listen(const std::string &bind_address,
                                    std::uint16_t port);
  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
  void close() noexcept;

  // 阻塞直到一个客户端完成或超时。测试用 localhost；板上 main 循环调用它。
  [[nodiscard]] Result<void>
  serve_one(std::chrono::milliseconds accept_timeout);

private:
  PhysicalModbusIoService &service_;
  OwnedFd listen_fd_{};
  std::uint16_t port_{0};
};

} // namespace rcr::workbench
