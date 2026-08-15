#pragma once

// ThinkPad 侧 commissioning 客户端。连接保持到 Disconnect；不打开 tty。
// disconnect() 可从 UI 线程调用：shutdown 唤醒 worker 里阻塞的 recv。

#include "rcr/owned_fd.hpp"
#include "rcr/result.hpp"
#include "rcr/workbench/application/modbus_agent_protocol.hpp"
#include "rcr/workbench/profile/mock_modbus_io_profile.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace rcr::workbench {

class ModbusAgentClient {
public:
  ~ModbusAgentClient();

  [[nodiscard]] Result<void> connect(const std::string &host, std::uint16_t port,
                                     std::chrono::milliseconds timeout);
  void disconnect() noexcept;
  [[nodiscard]] bool connected() const noexcept { return fd_.valid(); }

  [[nodiscard]] Result<ModbusIoSnapshot>
  probe(std::chrono::milliseconds timeout);
  [[nodiscard]] Result<ModbusIoSnapshot>
  read_inputs(std::chrono::milliseconds timeout);
  [[nodiscard]] Result<ModbusIoSnapshot>
  write_output(std::uint8_t channel, bool active,
               std::chrono::milliseconds timeout);
  [[nodiscard]] Result<ModbusIoSnapshot>
  write_all_outputs_off(std::chrono::milliseconds timeout);

private:
  [[nodiscard]] Result<ModbusIoSnapshot>
  exchange(ModbusAgentMessage request_type, ModbusAgentMessage ack_type,
           std::vector<std::uint8_t> payload, std::chrono::milliseconds timeout);

  OwnedFd fd_{};
  std::uint16_t next_sequence_{1};
};

} // namespace rcr::workbench
