#include "rcr/workbench/mock_actuator_profile.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace rcr::workbench {
namespace {

constexpr std::int64_t to_ns(std::chrono::nanoseconds duration) noexcept {
  return duration.count();
}

bool finite_positive(double value) noexcept {
  return std::isfinite(value) && value > 0.0;
}

} // namespace

MockActuatorProfile::MockActuatorProfile(MockActuatorConfig config)
    : config_(std::move(config)) {
  snapshot_.min_position_rad = config_.min_position_rad;
  snapshot_.max_position_rad = config_.max_position_rad;
  snapshot_.configured = configuration_valid();
  if (!snapshot_.configured) {
    raise_fault(ActuatorFaultReason::Configuration, true);
    snapshot_.last_reject = "invalid MockActuatorConfig";
  }
  update_snapshot_errors();
}

ActuatorSnapshot MockActuatorProfile::snapshot() const { return snapshot_; }

ActuatorCommandReply MockActuatorProfile::drive_enable() {
  const auto from = snapshot_.state;
  if (!snapshot_.configured) {
    return reject(CommandStatus::InvalidArgument, from,
                  "Mock actuator configuration is invalid");
  }
  if (snapshot_.state == ActuatorState::Fault) {
    return reject(CommandStatus::Rejected, from,
                  "Reset fault before Drive Enable");
  }
  if (snapshot_.state != ActuatorState::Disabled) {
    return reject(CommandStatus::Rejected, from,
                  "Drive Enable requires DISABLED");
  }
  snapshot_.drive_enabled = true;
  snapshot_.state =
      snapshot_.homed ? ActuatorState::Ready : ActuatorState::Idle;
  ++snapshot_.command_generation;
  return accept(from, "Drive enabled");
}

ActuatorCommandReply MockActuatorProfile::drive_disable() {
  const auto from = snapshot_.state;
  if (snapshot_.state == ActuatorState::Fault) {
    snapshot_.drive_enabled = false;
    return accept(from, "Drive output already revoked by fault");
  }
  if (snapshot_.state == ActuatorState::Disabled) {
    return reject(CommandStatus::Rejected, from, "Drive already disabled");
  }
  ++snapshot_.command_generation;
  if (std::abs(snapshot_.actual_velocity_rad_s) >
          config_.stopped_velocity_epsilon_rad_s ||
      snapshot_.state == ActuatorState::Running ||
      snapshot_.state == ActuatorState::Homing ||
      snapshot_.state == ActuatorState::Stopping) {
    snapshot_.homed = false;
    begin_stop(true, true);
    return accept(from, "Drive disable requested; quick deceleration active");
  }
  snapshot_.drive_enabled = false;
  snapshot_.homed = false;
  snapshot_.state = ActuatorState::Disabled;
  snapshot_.motion_mode = ActuatorMotionMode::None;
  return accept(from, "Drive disabled");
}

ActuatorCommandReply MockActuatorProfile::home() {
  const auto from = snapshot_.state;
  if (!snapshot_.drive_enabled || (snapshot_.state != ActuatorState::Idle &&
                                   snapshot_.state != ActuatorState::Ready)) {
    return reject(CommandStatus::Rejected, from,
                  "Home requires enabled IDLE or READY");
  }
  ++snapshot_.command_generation;
  snapshot_.homed = false;
  snapshot_.state = ActuatorState::Homing;
  snapshot_.motion_mode = ActuatorMotionMode::Homing;
  snapshot_.target_velocity_rad_s = config_.homing_velocity_rad_s;
  snapshot_.target_position_rad = snapshot_.actual_position_rad;
  homing_started_ns_ = monotonic_elapsed_ns_;
  tracking_error_count_ = 0;
  return accept(from, "MOCK homing started");
}

ActuatorCommandReply
MockActuatorProfile::start_velocity(double velocity_rad_s) {
  const auto from = snapshot_.state;
  if (!std::isfinite(velocity_rad_s) || velocity_rad_s == 0.0 ||
      std::abs(velocity_rad_s) > config_.max_velocity_rad_s) {
    return reject(
        CommandStatus::InvalidArgument, from,
        "Velocity must be finite, nonzero, and within configured max");
  }
  if (!snapshot_.drive_enabled || !snapshot_.homed ||
      snapshot_.state != ActuatorState::Ready) {
    return reject(CommandStatus::Rejected, from,
                  "Velocity start requires enabled, homed READY state");
  }
  if ((velocity_rad_s > 0.0 &&
       snapshot_.actual_position_rad >= config_.max_position_rad) ||
      (velocity_rad_s < 0.0 &&
       snapshot_.actual_position_rad <= config_.min_position_rad)) {
    raise_fault(velocity_rad_s > 0.0 ? ActuatorFaultReason::SoftLimitPositive
                                     : ActuatorFaultReason::SoftLimitNegative,
                false);
    return reject(CommandStatus::Rejected, from,
                  "Motion direction violates active soft limit");
  }
  ++snapshot_.command_generation;
  snapshot_.state = ActuatorState::Running;
  snapshot_.motion_mode = ActuatorMotionMode::Velocity;
  snapshot_.target_velocity_rad_s = velocity_rad_s;
  snapshot_.target_position_rad = snapshot_.actual_position_rad;
  tracking_error_count_ = 0;
  return accept(from, "Velocity motion started");
}

ActuatorCommandReply MockActuatorProfile::normal_stop() {
  const auto from = snapshot_.state;
  if (snapshot_.state == ActuatorState::Stopping) {
    return accept(from, "Stop already active; existing deceleration retained");
  }
  if (snapshot_.state != ActuatorState::Running &&
      snapshot_.state != ActuatorState::Homing) {
    return reject(CommandStatus::Rejected, from,
                  "Normal Stop requires active motion");
  }
  ++snapshot_.command_generation;
  begin_stop(false);
  return accept(from, "Normal Stop requested");
}

ActuatorCommandReply MockActuatorProfile::quick_stop() {
  const auto from = snapshot_.state;
  if (snapshot_.state != ActuatorState::Running &&
      snapshot_.state != ActuatorState::Homing &&
      snapshot_.state != ActuatorState::Stopping) {
    return reject(CommandStatus::Rejected, from,
                  "Quick Stop has no active software motion");
  }
  ++snapshot_.command_generation;
  begin_stop(true);
  return accept(from, "QUICK STOP requested (software only)");
}

ActuatorCommandReply MockActuatorProfile::jog_press(int direction,
                                                    double velocity_rad_s) {
  const auto from = snapshot_.state;
  if ((direction != -1 && direction != 1) || !finite_positive(velocity_rad_s) ||
      velocity_rad_s > config_.max_velocity_rad_s) {
    return reject(CommandStatus::InvalidArgument, from,
                  "Jog requires direction -1/+1 and valid positive velocity");
  }
  if (!snapshot_.drive_enabled || !snapshot_.homed ||
      snapshot_.state != ActuatorState::Ready) {
    return reject(CommandStatus::Rejected, from,
                  "Jog requires enabled, homed READY state");
  }
  const double signed_velocity =
      direction > 0 ? velocity_rad_s : -velocity_rad_s;
  if ((direction > 0 &&
       snapshot_.actual_position_rad >= config_.max_position_rad) ||
      (direction < 0 &&
       snapshot_.actual_position_rad <= config_.min_position_rad)) {
    raise_fault(direction > 0 ? ActuatorFaultReason::SoftLimitPositive
                              : ActuatorFaultReason::SoftLimitNegative,
                false);
    return reject(CommandStatus::Rejected, from,
                  "Jog direction violates active soft limit");
  }
  ++snapshot_.command_generation;
  snapshot_.active_jog_token = snapshot_.command_generation;
  snapshot_.state = ActuatorState::Running;
  snapshot_.motion_mode = direction > 0 ? ActuatorMotionMode::JogPositive
                                        : ActuatorMotionMode::JogNegative;
  snapshot_.target_velocity_rad_s = signed_velocity;
  snapshot_.target_position_rad = snapshot_.actual_position_rad;
  jog_deadline_ns_ = monotonic_elapsed_ns_ + to_ns(config_.jog_deadman);
  jog_max_deadline_ns_ =
      monotonic_elapsed_ns_ + to_ns(config_.max_continuous_jog);
  tracking_error_count_ = 0;
  return accept(from, "Jog lease started", snapshot_.active_jog_token);
}

ActuatorCommandReply MockActuatorProfile::jog_renew(std::uint64_t token) {
  const auto from = snapshot_.state;
  if (token == 0 || token != snapshot_.active_jog_token ||
      snapshot_.state != ActuatorState::Running ||
      (snapshot_.motion_mode != ActuatorMotionMode::JogPositive &&
       snapshot_.motion_mode != ActuatorMotionMode::JogNegative)) {
    return reject(CommandStatus::Rejected, from,
                  "Jog renew token is stale or no jog is active");
  }
  if (monotonic_elapsed_ns_ >= jog_max_deadline_ns_) {
    begin_stop(false);
    return reject(CommandStatus::Timeout, from,
                  "Maximum continuous jog duration reached");
  }
  jog_deadline_ns_ = std::min(
      monotonic_elapsed_ns_ + to_ns(config_.jog_deadman), jog_max_deadline_ns_);
  return accept(from, "Jog lease renewed", token);
}

ActuatorCommandReply MockActuatorProfile::jog_release(std::uint64_t token) {
  const auto from = snapshot_.state;
  if (token == 0 || token != snapshot_.active_jog_token) {
    return reject(CommandStatus::Rejected, from, "Jog release token is stale");
  }
  ++snapshot_.command_generation;
  begin_stop(false);
  return accept(from, "Jog released; Normal Stop active");
}

ActuatorCommandReply MockActuatorProfile::reset_fault() {
  const auto from = snapshot_.state;
  if (snapshot_.state != ActuatorState::Fault) {
    return reject(CommandStatus::Rejected, from, "No actuator fault to reset");
  }
  if (!snapshot_.configured || persistent_blocker_) {
    return reject(CommandStatus::Rejected, from,
                  "Persistent fault blocker is still active");
  }
  if (std::abs(snapshot_.actual_velocity_rad_s) >
      config_.stopped_velocity_epsilon_rad_s) {
    return reject(CommandStatus::Busy, from,
                  "Wait for neutral velocity before fault reset");
  }
  ++snapshot_.command_generation;
  snapshot_.fault = ActuatorFaultReason::None;
  snapshot_.drive_enabled = false;
  snapshot_.homed = false;
  snapshot_.state = ActuatorState::Disabled;
  snapshot_.motion_mode = ActuatorMotionMode::None;
  snapshot_.target_velocity_rad_s = 0.0;
  snapshot_.target_position_rad = snapshot_.actual_position_rad;
  snapshot_.active_jog_token = 0;
  tracking_error_count_ = 0;
  return accept(from, "Fault reset to safe DISABLED state");
}

void MockActuatorProfile::inject_fault(ActuatorFaultReason reason,
                                       bool persistent) {
  if (reason == ActuatorFaultReason::None) {
    return;
  }
  raise_fault(reason, persistent);
}

void MockActuatorProfile::clear_injected_blocker() {
  persistent_blocker_ = false;
}

void MockActuatorProfile::tick(std::chrono::nanoseconds elapsed) {
  if (elapsed <= std::chrono::nanoseconds::zero()) {
    return;
  }
  const auto elapsed_count = elapsed.count();
  if (elapsed_count >
      std::numeric_limits<std::int64_t>::max() - monotonic_elapsed_ns_) {
    monotonic_elapsed_ns_ = std::numeric_limits<std::int64_t>::max();
  } else {
    monotonic_elapsed_ns_ += elapsed_count;
  }

  if (snapshot_.state == ActuatorState::Running &&
      snapshot_.active_jog_token != 0 &&
      (monotonic_elapsed_ns_ >= jog_deadline_ns_ ||
       monotonic_elapsed_ns_ >= jog_max_deadline_ns_)) {
    begin_stop(false);
    snapshot_.last_reject = "Jog deadman timeout; Normal Stop active";
  }

  if (snapshot_.state == ActuatorState::Homing &&
      monotonic_elapsed_ns_ - homing_started_ns_ >=
          to_ns(config_.homing_duration)) {
    snapshot_.actual_position_rad = 0.0;
    snapshot_.target_position_rad = 0.0;
    snapshot_.actual_velocity_rad_s = 0.0;
    snapshot_.target_velocity_rad_s = 0.0;
    snapshot_.homed = true;
    snapshot_.state = ActuatorState::Ready;
    snapshot_.motion_mode = ActuatorMotionMode::None;
    update_snapshot_errors();
    return;
  }

  const auto physics_step =
      std::min(elapsed, std::chrono::duration_cast<std::chrono::nanoseconds>(
                            config_.max_physics_step));
  const double dt = std::chrono::duration<double>(physics_step).count();
  double response = config_.velocity_response_per_s;
  if (snapshot_.state == ActuatorState::Stopping ||
      snapshot_.state == ActuatorState::Fault) {
    response = quick_stopping_ ? config_.quick_stop_response_per_s
                               : config_.normal_stop_response_per_s;
  }
  const double alpha = std::clamp(response * dt, 0.0, 1.0);
  snapshot_.actual_velocity_rad_s += alpha * (snapshot_.target_velocity_rad_s -
                                              snapshot_.actual_velocity_rad_s);
  snapshot_.target_position_rad += snapshot_.target_velocity_rad_s * dt;
  snapshot_.actual_position_rad += snapshot_.actual_velocity_rad_s * dt;

  if (snapshot_.target_position_rad > config_.max_position_rad ||
      snapshot_.actual_position_rad > config_.max_position_rad) {
    snapshot_.target_position_rad = config_.max_position_rad;
    snapshot_.actual_position_rad =
        std::min(snapshot_.actual_position_rad, config_.max_position_rad);
    raise_fault(ActuatorFaultReason::SoftLimitPositive, false);
  } else if (snapshot_.target_position_rad < config_.min_position_rad ||
             snapshot_.actual_position_rad < config_.min_position_rad) {
    snapshot_.target_position_rad = config_.min_position_rad;
    snapshot_.actual_position_rad =
        std::max(snapshot_.actual_position_rad, config_.min_position_rad);
    raise_fault(ActuatorFaultReason::SoftLimitNegative, false);
  }

  update_snapshot_errors();
  if (snapshot_.state == ActuatorState::Running) {
    if (std::abs(snapshot_.position_error_rad) >
        config_.tracking_error_threshold_rad) {
      ++tracking_error_count_;
      if (tracking_error_count_ >= config_.tracking_error_cycles) {
        raise_fault(ActuatorFaultReason::TrackingError, false);
      }
    } else {
      tracking_error_count_ = 0;
    }
  } else {
    tracking_error_count_ = 0;
  }

  if (snapshot_.state == ActuatorState::Stopping &&
      std::abs(snapshot_.actual_velocity_rad_s) <=
          config_.stopped_velocity_epsilon_rad_s) {
    snapshot_.actual_velocity_rad_s = 0.0;
    snapshot_.target_velocity_rad_s = 0.0;
    snapshot_.target_position_rad = snapshot_.actual_position_rad;
    snapshot_.motion_mode = ActuatorMotionMode::None;
    if (disable_after_stop_) {
      snapshot_.drive_enabled = false;
      snapshot_.homed = false;
      snapshot_.state = ActuatorState::Disabled;
    } else {
      snapshot_.state =
          snapshot_.homed ? ActuatorState::Ready : ActuatorState::Idle;
    }
    disable_after_stop_ = false;
    quick_stopping_ = false;
  }
  update_snapshot_errors();
}

ActuatorCommandReply MockActuatorProfile::accept(ActuatorState from,
                                                 std::string message,
                                                 std::uint64_t token) {
  snapshot_.last_reject.clear();
  return {CommandStatus::Accepted, from, snapshot_.state, token,
          std::move(message)};
}

ActuatorCommandReply MockActuatorProfile::reject(CommandStatus status,
                                                 ActuatorState from,
                                                 std::string message) {
  snapshot_.last_reject = message;
  return {status, from, snapshot_.state, 0, std::move(message)};
}

void MockActuatorProfile::begin_stop(bool quick, bool disable_after_stop) {
  snapshot_.target_velocity_rad_s = 0.0;
  snapshot_.active_jog_token = 0;
  snapshot_.state = ActuatorState::Stopping;
  snapshot_.motion_mode = ActuatorMotionMode::None;
  quick_stopping_ = quick;
  disable_after_stop_ = disable_after_stop;
}

void MockActuatorProfile::raise_fault(ActuatorFaultReason reason,
                                      bool persistent) {
  snapshot_.fault = reason;
  snapshot_.state = ActuatorState::Fault;
  snapshot_.drive_enabled = false;
  snapshot_.homed = false;
  snapshot_.target_velocity_rad_s = 0.0;
  snapshot_.active_jog_token = 0;
  snapshot_.motion_mode = ActuatorMotionMode::None;
  quick_stopping_ = true;
  disable_after_stop_ = false;
  persistent_blocker_ = persistent_blocker_ || persistent;
  snapshot_.last_reject = std::string(to_string(reason));
}

void MockActuatorProfile::update_snapshot_errors() {
  snapshot_.position_error_rad =
      snapshot_.target_position_rad - snapshot_.actual_position_rad;
  snapshot_.velocity_error_rad_s =
      snapshot_.target_velocity_rad_s - snapshot_.actual_velocity_rad_s;
}

bool MockActuatorProfile::configuration_valid() const noexcept {
  return std::isfinite(config_.min_position_rad) &&
         std::isfinite(config_.max_position_rad) &&
         config_.min_position_rad < config_.max_position_rad &&
         finite_positive(config_.max_velocity_rad_s) &&
         finite_positive(config_.velocity_response_per_s) &&
         finite_positive(config_.normal_stop_response_per_s) &&
         finite_positive(config_.quick_stop_response_per_s) &&
         std::isfinite(config_.homing_velocity_rad_s) &&
         std::abs(config_.homing_velocity_rad_s) <=
             config_.max_velocity_rad_s &&
         config_.homing_duration > std::chrono::milliseconds::zero() &&
         config_.jog_deadman > std::chrono::milliseconds::zero() &&
         config_.max_continuous_jog >= config_.jog_deadman &&
         config_.max_physics_step > std::chrono::milliseconds::zero() &&
         finite_positive(config_.tracking_error_threshold_rad) &&
         config_.tracking_error_cycles > 0 &&
         finite_positive(config_.stopped_velocity_epsilon_rad_s);
}

} // namespace rcr::workbench
