#pragma once

#include "rcr/workbench/application/application_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rcr::workbench {

inline constexpr std::size_t kModbusIoChannelCount = 4;
inline constexpr std::uint8_t kAllModbusIoChannels = 0xFF;

enum class ModbusScanState : std::uint8_t {
  Unknown = 0,
  Scanning,
  Complete,
  Error,
};

[[nodiscard]] constexpr std::string_view
to_string(ModbusScanState state) noexcept {
  switch (state) {
  case ModbusScanState::Unknown:
    return "UNKNOWN";
  case ModbusScanState::Scanning:
    return "SCANNING";
  case ModbusScanState::Complete:
    return "COMPLETE";
  case ModbusScanState::Error:
    return "ERROR";
  }
  return "UNKNOWN";
}

enum class ModbusDeviceState : std::uint8_t {
  Unknown = 0,
  Online,
  Timeout,
  Error,
};

[[nodiscard]] constexpr std::string_view
to_string(ModbusDeviceState state) noexcept {
  switch (state) {
  case ModbusDeviceState::Unknown:
    return "UNKNOWN";
  case ModbusDeviceState::Online:
    return "ONLINE";
  case ModbusDeviceState::Timeout:
    return "TIMEOUT";
  case ModbusDeviceState::Error:
    return "ERROR";
  }
  return "UNKNOWN";
}

enum class ModbusIoCommandStatus : std::uint8_t {
  None = 0,
  Confirmed,
  Timeout,
  Exception,
  Rejected,
  InvalidChannel,
  Busy,
};

[[nodiscard]] constexpr std::string_view
to_string(ModbusIoCommandStatus status) noexcept {
  switch (status) {
  case ModbusIoCommandStatus::None:
    return "NONE";
  case ModbusIoCommandStatus::Confirmed:
    return "CONFIRMED";
  case ModbusIoCommandStatus::Timeout:
    return "TIMEOUT";
  case ModbusIoCommandStatus::Exception:
    return "EXCEPTION";
  case ModbusIoCommandStatus::Rejected:
    return "REJECTED";
  case ModbusIoCommandStatus::InvalidChannel:
    return "INVALID_CHANNEL";
  case ModbusIoCommandStatus::Busy:
    return "BUSY";
  }
  return "UNKNOWN";
}

struct ModbusSlaveSummary {
  std::uint8_t slave_id{0};
  ModbusDeviceState state{ModbusDeviceState::Unknown};
  std::string detail{};
};

struct DigitalInputState {
  std::uint8_t channel{0};
  bool active{false};
};

struct DigitalOutputState {
  std::uint8_t channel{0};
  bool requested{false};
  bool confirmed{false};
  ModbusIoCommandStatus last_status{ModbusIoCommandStatus::None};
};

// 最近一笔 RTU/agent 事务的展示字段。Mock 保持空；Physical 由板上主站填写。
struct ModbusTransaction {
  std::string direction{"NONE"};
  std::uint8_t slave_id{0};
  std::uint8_t function{0};
  std::uint16_t address{0};
  std::uint16_t quantity{0};
  std::string result{};
  std::int64_t rtt_ns{0};
  std::string tx_hex{};
  std::string rx_hex{};
};

struct ModbusIoSnapshot {
  std::string backend{"MOCK"};
  EvidenceClass evidence{EvidenceClass::Mock};
  bool no_physical_rs485{true};
  std::string transport{"Modbus RTU (planned)"};
  std::string serial_port{"NOT CONNECTED"};
  std::string agent_peer{"n/a"};
  std::string sku{};
  std::uint32_t baud_rate_placeholder{9600};
  std::uint32_t baud_rate{9600};
  std::string parity_placeholder{"None (placeholder)"};
  std::string parity{"None"};
  std::uint8_t slave_id{1};
  ModbusScanState scan_state{ModbusScanState::Unknown};
  ModbusDeviceState device_state{ModbusDeviceState::Online};
  std::array<DigitalInputState, kModbusIoChannelCount> digital_inputs{};
  std::array<DigitalOutputState, kModbusIoChannelCount> digital_outputs{};
  std::vector<ModbusSlaveSummary> slaves{};
  ModbusIoCommandStatus last_command_status{ModbusIoCommandStatus::None};
  ModbusTransaction last_transaction{};
  std::int64_t last_update_monotonic_ns{0};
  std::string last_error{};
};

struct ModbusIoCommandReply {
  ModbusIoCommandStatus status{ModbusIoCommandStatus::Rejected};
  std::uint8_t channel{kAllModbusIoChannels};
  bool requested{false};
  bool confirmed{false};
  std::string message{};

  [[nodiscard]] bool accepted() const noexcept {
    return status == ModbusIoCommandStatus::Confirmed;
  }
};

struct MockModbusIoConfig {
  std::uint8_t primary_slave_id{1};
  std::vector<ModbusSlaveSummary> scan_results{
      {1, ModbusDeviceState::Online, "MOCK ONLINE"},
      {2, ModbusDeviceState::Timeout, "MOCK TIMEOUT"},
      {3, ModbusDeviceState::Online, "MOCK ONLINE"},
  };
};

/**
 * 4 DI / 4 DO 的确定性 Modbus I/O Mock。
 *
 * 对象不建线程、不访问墙钟、串口或 Runtime。调用者传入单调时间；扫描分
 * begin/complete 两步，避免未来把真实阻塞扫描写进
 * MainWindow。故障注入只影响下一笔 DO 请求并自动复位。
 */
class MockModbusIoProfile {
public:
  explicit MockModbusIoProfile(MockModbusIoConfig config = {});

  [[nodiscard]] ModbusIoSnapshot snapshot() const;
  [[nodiscard]] bool begin_scan(std::int64_t now_ns);
  [[nodiscard]] bool complete_scan(std::int64_t now_ns);
  [[nodiscard]] ModbusIoCommandReply
  set_mock_digital_input(std::size_t channel, bool active, std::int64_t now_ns);
  [[nodiscard]] ModbusIoCommandReply
  write_digital_output(std::size_t channel, bool active, std::int64_t now_ns);
  [[nodiscard]] ModbusIoCommandReply write_all_outputs_off(std::int64_t now_ns);

  void set_next_write_outcome(ModbusIoCommandStatus outcome);

private:
  [[nodiscard]] bool update_time(std::int64_t now_ns);
  [[nodiscard]] ModbusIoCommandStatus consume_write_outcome();
  [[nodiscard]] ModbusIoCommandReply invalid_channel(std::size_t channel,
                                                     std::int64_t now_ns);
  [[nodiscard]] ModbusIoCommandReply
  finish_write(std::uint8_t channel, bool requested, bool confirmed_before,
               ModbusIoCommandStatus outcome);

  MockModbusIoConfig config_{};
  ModbusIoSnapshot snapshot_{};
  ModbusIoCommandStatus next_write_outcome_{ModbusIoCommandStatus::Confirmed};
};

} // namespace rcr::workbench
