#pragma once

#include "rcr/workbench/application_model.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace rcr::workbench {

enum class ActuatorState : std::uint8_t {
  Disabled = 0,
  Idle,
  Homing,
  Ready,
  Running,
  Stopping,
  Fault,
};

[[nodiscard]] constexpr std::string_view
to_string(ActuatorState state) noexcept {
  switch (state) {
  case ActuatorState::Disabled:
    return "DISABLED";
  case ActuatorState::Idle:
    return "IDLE";
  case ActuatorState::Homing:
    return "HOMING";
  case ActuatorState::Ready:
    return "READY";
  case ActuatorState::Running:
    return "RUNNING";
  case ActuatorState::Stopping:
    return "STOPPING";
  case ActuatorState::Fault:
    return "FAULT";
  }
  return "UNKNOWN";
}

enum class ActuatorMotionMode : std::uint8_t {
  None = 0,
  Velocity,
  JogPositive,
  JogNegative,
  Homing,
};

[[nodiscard]] constexpr std::string_view
to_string(ActuatorMotionMode mode) noexcept {
  switch (mode) {
  case ActuatorMotionMode::None:
    return "NONE";
  case ActuatorMotionMode::Velocity:
    return "VELOCITY";
  case ActuatorMotionMode::JogPositive:
    return "JOG_POSITIVE";
  case ActuatorMotionMode::JogNegative:
    return "JOG_NEGATIVE";
  case ActuatorMotionMode::Homing:
    return "HOMING";
  }
  return "UNKNOWN";
}

enum class ActuatorFaultReason : std::uint8_t {
  None = 0,
  Configuration,
  SoftLimitPositive,
  SoftLimitNegative,
  TrackingError,
  EncoderFault,
  CommunicationTimeout,
  DeviceFault,
};

[[nodiscard]] constexpr std::string_view
to_string(ActuatorFaultReason reason) noexcept {
  switch (reason) {
  case ActuatorFaultReason::None:
    return "NONE";
  case ActuatorFaultReason::Configuration:
    return "CONFIGURATION";
  case ActuatorFaultReason::SoftLimitPositive:
    return "SOFT_LIMIT_POSITIVE";
  case ActuatorFaultReason::SoftLimitNegative:
    return "SOFT_LIMIT_NEGATIVE";
  case ActuatorFaultReason::TrackingError:
    return "TRACKING_ERROR";
  case ActuatorFaultReason::EncoderFault:
    return "ENCODER_FAULT";
  case ActuatorFaultReason::CommunicationTimeout:
    return "COMMUNICATION_TIMEOUT";
  case ActuatorFaultReason::DeviceFault:
    return "DEVICE_FAULT";
  }
  return "UNKNOWN";
}

struct MockActuatorConfig {
  double min_position_rad{-2.8};
  double max_position_rad{2.8};
  double max_velocity_rad_s{2.0};
  double velocity_response_per_s{8.0};
  double normal_stop_response_per_s{12.0};
  double quick_stop_response_per_s{40.0};
  double homing_velocity_rad_s{-0.25};
  std::chrono::milliseconds homing_duration{500};
  std::chrono::milliseconds jog_deadman{200};
  std::chrono::milliseconds max_continuous_jog{2000};
  std::chrono::milliseconds max_physics_step{50};
  double tracking_error_threshold_rad{1.0};
  std::uint32_t tracking_error_cycles{50};
  double stopped_velocity_epsilon_rad_s{0.01};
};

struct ActuatorSnapshot {
  std::string device_id{"ACTUATOR_01"};
  EvidenceClass evidence{EvidenceClass::Mock};
  bool isolated_mock{true};
  bool configured{true};
  ActuatorState state{ActuatorState::Disabled};
  ActuatorMotionMode motion_mode{ActuatorMotionMode::None};
  ActuatorFaultReason fault{ActuatorFaultReason::None};
  bool drive_enabled{false};
  bool homed{false};
  double target_position_rad{0.0};
  double actual_position_rad{0.0};
  double position_error_rad{0.0};
  double target_velocity_rad_s{0.0};
  double actual_velocity_rad_s{0.0};
  double velocity_error_rad_s{0.0};
  double min_position_rad{-2.8};
  double max_position_rad{2.8};
  std::uint64_t command_generation{0};
  std::uint64_t active_jog_token{0};
  std::string last_reject{};
};

struct ActuatorCommandReply {
  CommandStatus status{CommandStatus::Rejected};
  ActuatorState from_state{ActuatorState::Disabled};
  ActuatorState to_state{ActuatorState::Disabled};
  std::uint64_t token{0};
  std::string message{};

  [[nodiscard]] bool accepted() const noexcept {
    return status == CommandStatus::Accepted;
  }
};

/**
 * 单轴确定性模拟 profile。
 *
 * 不创建线程、不读取墙钟、不拥有 Runtime/CAN；调用者显式 tick，便于无 sleep
 * 单测。 它是 commissioning 教学模型，不是物理 servo 或安全控制器。
 */
class MockActuatorProfile {
public:
  explicit MockActuatorProfile(MockActuatorConfig config = {});

  [[nodiscard]] ActuatorSnapshot snapshot() const;
  [[nodiscard]] ActuatorCommandReply drive_enable();
  [[nodiscard]] ActuatorCommandReply drive_disable();
  [[nodiscard]] ActuatorCommandReply home();
  [[nodiscard]] ActuatorCommandReply start_velocity(double velocity_rad_s);
  [[nodiscard]] ActuatorCommandReply normal_stop();
  [[nodiscard]] ActuatorCommandReply quick_stop();
  [[nodiscard]] ActuatorCommandReply jog_press(int direction,
                                               double velocity_rad_s);
  [[nodiscard]] ActuatorCommandReply jog_renew(std::uint64_t token);
  [[nodiscard]] ActuatorCommandReply jog_release(std::uint64_t token);
  [[nodiscard]] ActuatorCommandReply reset_fault();

  void inject_fault(ActuatorFaultReason reason, bool persistent = true);
  void clear_injected_blocker();
  void tick(std::chrono::nanoseconds elapsed);

private:
  [[nodiscard]] ActuatorCommandReply
  accept(ActuatorState from, std::string message, std::uint64_t token = 0);
  [[nodiscard]] ActuatorCommandReply
  reject(CommandStatus status, ActuatorState from, std::string message);
  void begin_stop(bool quick, bool disable_after_stop = false);
  void raise_fault(ActuatorFaultReason reason, bool persistent);
  void update_snapshot_errors();
  [[nodiscard]] bool configuration_valid() const noexcept;

  MockActuatorConfig config_{};
  ActuatorSnapshot snapshot_{};
  std::int64_t monotonic_elapsed_ns_{0};
  std::int64_t homing_started_ns_{0};
  std::int64_t jog_deadline_ns_{0};
  std::int64_t jog_max_deadline_ns_{0};
  std::uint32_t tracking_error_count_{0};
  bool quick_stopping_{false};
  bool disable_after_stop_{false};
  bool persistent_blocker_{false};
};

} // namespace rcr::workbench
