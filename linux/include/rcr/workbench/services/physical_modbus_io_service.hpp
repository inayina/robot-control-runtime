#pragma once

// MR0-IOR08 设备语义：probe / 读 DI / FC05 写线圈。阻塞 RTU 事务由调用方线程执行。
// 默认走 POSIX 串口；测试可注入 transact，不必造完整假 tty。
// 写成功才把 confirmed 置位；timeout 不重试、不把 requested 当成已落地。

#include "rcr/result.hpp"
#include "rcr/workbench/profile/mock_modbus_io_profile.hpp"
#include "rcr/workbench/services/posix_serial_port.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace rcr::workbench {

struct PhysicalModbusIoConfig {
  std::string serial_port{"/dev/ttyS7"};
  std::uint32_t baud_rate{9600};
  char parity{'N'};
  std::uint8_t slave_id{1};
  std::string sku{"MR0-IOR08"};
  std::chrono::milliseconds timeout{200};
};

using RtuTransact = std::function<Result<std::vector<std::uint8_t>>(
    std::span<const std::uint8_t>, std::chrono::milliseconds)>;

class PhysicalModbusIoService {
public:
  explicit PhysicalModbusIoService(PhysicalModbusIoConfig config = {},
                                   RtuTransact transact = {});

  [[nodiscard]] ModbusIoSnapshot snapshot() const { return snapshot_; }
  [[nodiscard]] Result<ModbusIoSnapshot> probe();
  [[nodiscard]] Result<ModbusIoSnapshot> read_inputs();
  [[nodiscard]] Result<ModbusIoSnapshot> write_output(std::size_t channel,
                                                      bool active);
  [[nodiscard]] Result<ModbusIoSnapshot> write_all_outputs_off();
  void disconnect() noexcept;

private:
  void apply_device_identity();
  void begin_transaction(std::uint8_t function, std::uint16_t address,
                         std::uint16_t quantity,
                         std::span<const std::uint8_t> tx);
  void apply_fault(const Error &error);
  [[nodiscard]] Result<std::vector<std::uint8_t>>
  transact(std::span<const std::uint8_t> request);
  [[nodiscard]] Result<ModbusIoSnapshot> read_discrete_inputs(bool scanning);
  [[nodiscard]] Result<void> refresh_coils();

  PhysicalModbusIoConfig config_{};
  RtuTransact transact_{};
  PosixSerialPort serial_{};
  bool using_injected_transact_{false};
  ModbusIoSnapshot snapshot_{};
};

} // namespace rcr::workbench
