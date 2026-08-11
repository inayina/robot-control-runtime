#include "rcr/workbench/profile/mock_actuator_profile.hpp"

#include "test_support.hpp"

#include <chrono>
#include <cmath>

using namespace std::chrono_literals;

namespace {

using rcr::workbench::ActuatorFaultReason;
using rcr::workbench::ActuatorState;
using rcr::workbench::MockActuatorConfig;
using rcr::workbench::MockActuatorProfile;

void advance(MockActuatorProfile &profile, std::chrono::milliseconds duration,
             std::chrono::milliseconds step = 10ms) {
  for (auto elapsed = 0ms; elapsed < duration; elapsed += step) {
    profile.tick(step);
  }
}

void make_ready(MockActuatorProfile &profile) {
  RCR_REQUIRE(profile.drive_enable().accepted());
  RCR_REQUIRE(profile.home().accepted());
  advance(profile, 500ms);
  const auto snapshot = profile.snapshot();
  RCR_REQUIRE(snapshot.state == ActuatorState::Ready);
  RCR_REQUIRE(snapshot.homed);
}

} // namespace

RCR_TEST(invalid_configuration_fails_closed) {
  MockActuatorConfig config{};
  config.min_position_rad = 1.0;
  config.max_position_rad = 1.0;
  MockActuatorProfile profile{config};

  const auto snapshot = profile.snapshot();
  RCR_EXPECT(!snapshot.configured);
  RCR_EXPECT(snapshot.state == ActuatorState::Fault);
  RCR_EXPECT(snapshot.fault == ActuatorFaultReason::Configuration);
  RCR_EXPECT(!profile.drive_enable().accepted());
}

RCR_TEST(enable_and_illegal_start_are_explicit) {
  MockActuatorProfile profile;
  RCR_EXPECT(!profile.start_velocity(1.0).accepted());
  RCR_REQUIRE(profile.drive_enable().accepted());
  RCR_EXPECT(profile.snapshot().state == ActuatorState::Idle);
  RCR_EXPECT(!profile.start_velocity(1.0).accepted());
}

RCR_TEST(homing_reaches_zero_and_ready) {
  MockActuatorProfile profile;
  RCR_REQUIRE(profile.drive_enable().accepted());
  RCR_REQUIRE(profile.home().accepted());
  RCR_EXPECT(profile.snapshot().state == ActuatorState::Homing);
  advance(profile, 490ms);
  RCR_EXPECT(profile.snapshot().state == ActuatorState::Homing);
  profile.tick(10ms);
  const auto snapshot = profile.snapshot();
  RCR_EXPECT(snapshot.state == ActuatorState::Ready);
  RCR_EXPECT(snapshot.homed);
  RCR_EXPECT(snapshot.actual_position_rad == 0.0);
}

RCR_TEST(velocity_converges_and_normal_stop_reaches_ready) {
  MockActuatorProfile profile;
  make_ready(profile);
  RCR_REQUIRE(profile.start_velocity(1.0).accepted());
  advance(profile, 400ms);
  RCR_EXPECT(profile.snapshot().actual_velocity_rad_s > 0.9);
  RCR_REQUIRE(profile.normal_stop().accepted());
  advance(profile, 500ms);
  RCR_EXPECT(profile.snapshot().state == ActuatorState::Ready);
  RCR_EXPECT(profile.snapshot().actual_velocity_rad_s == 0.0);
}

RCR_TEST(quick_stop_decelerates_faster_than_normal_stop) {
  MockActuatorProfile normal;
  MockActuatorProfile quick;
  make_ready(normal);
  make_ready(quick);
  RCR_REQUIRE(normal.start_velocity(1.0).accepted());
  RCR_REQUIRE(quick.start_velocity(1.0).accepted());
  advance(normal, 300ms);
  advance(quick, 300ms);
  RCR_REQUIRE(normal.normal_stop().accepted());
  RCR_REQUIRE(quick.quick_stop().accepted());
  normal.tick(10ms);
  quick.tick(10ms);
  RCR_EXPECT(std::abs(quick.snapshot().actual_velocity_rad_s) <
             std::abs(normal.snapshot().actual_velocity_rad_s));
}

RCR_TEST(normal_stop_cannot_downgrade_an_active_quick_stop) {
  MockActuatorProfile baseline;
  MockActuatorProfile repeated;
  make_ready(baseline);
  make_ready(repeated);
  RCR_REQUIRE(baseline.start_velocity(1.0).accepted());
  RCR_REQUIRE(repeated.start_velocity(1.0).accepted());
  advance(baseline, 300ms);
  advance(repeated, 300ms);
  RCR_REQUIRE(baseline.quick_stop().accepted());
  RCR_REQUIRE(repeated.quick_stop().accepted());
  RCR_REQUIRE(repeated.normal_stop().accepted());
  baseline.tick(10ms);
  repeated.tick(10ms);
  RCR_EXPECT(std::abs(baseline.snapshot().actual_velocity_rad_s -
                      repeated.snapshot().actual_velocity_rad_s) < 1e-12);
}

RCR_TEST(jog_directions_and_release_are_bounded) {
  MockActuatorProfile positive;
  make_ready(positive);
  const auto plus = positive.jog_press(1, 0.5);
  RCR_REQUIRE(plus.accepted());
  advance(positive, 100ms);
  RCR_EXPECT(positive.snapshot().actual_position_rad > 0.0);
  RCR_REQUIRE(positive.jog_release(plus.token).accepted());
  advance(positive, 500ms);
  RCR_EXPECT(positive.snapshot().state == ActuatorState::Ready);

  MockActuatorProfile negative;
  make_ready(negative);
  const auto minus = negative.jog_press(-1, 0.5);
  RCR_REQUIRE(minus.accepted());
  advance(negative, 100ms);
  RCR_EXPECT(negative.snapshot().actual_position_rad < 0.0);
}

RCR_TEST(jog_release_loss_hits_deadman_and_stale_token_is_rejected) {
  MockActuatorProfile profile;
  make_ready(profile);
  const auto jog = profile.jog_press(1, 0.5);
  RCR_REQUIRE(jog.accepted());
  advance(profile, 210ms);
  RCR_EXPECT(profile.snapshot().state == ActuatorState::Stopping);
  RCR_EXPECT(profile.snapshot().active_jog_token == 0);
  RCR_EXPECT(!profile.jog_renew(jog.token).accepted());
  advance(profile, 500ms);
  RCR_EXPECT(profile.snapshot().state == ActuatorState::Ready);
}

RCR_TEST(jog_renew_cannot_exceed_maximum_continuous_duration) {
  MockActuatorConfig config{};
  config.max_continuous_jog = 300ms;
  MockActuatorProfile profile{config};
  make_ready(profile);
  const auto jog = profile.jog_press(1, 0.5);
  RCR_REQUIRE(jog.accepted());
  for (int index = 0; index < 7; ++index) {
    profile.tick(50ms);
    (void)profile.jog_renew(jog.token);
  }
  RCR_EXPECT(profile.snapshot().state == ActuatorState::Stopping);
}

RCR_TEST(positive_and_negative_soft_limits_fault_without_overshoot) {
  MockActuatorConfig config{};
  config.min_position_rad = -0.2;
  config.max_position_rad = 0.2;
  config.tracking_error_threshold_rad = 10.0;

  MockActuatorProfile positive{config};
  make_ready(positive);
  RCR_REQUIRE(positive.start_velocity(1.0).accepted());
  advance(positive, 500ms);
  RCR_EXPECT(positive.snapshot().state == ActuatorState::Fault);
  RCR_EXPECT(positive.snapshot().fault ==
             ActuatorFaultReason::SoftLimitPositive);
  RCR_EXPECT(positive.snapshot().actual_position_rad <=
             config.max_position_rad);

  MockActuatorProfile negative{config};
  make_ready(negative);
  RCR_REQUIRE(negative.start_velocity(-1.0).accepted());
  advance(negative, 500ms);
  RCR_EXPECT(negative.snapshot().state == ActuatorState::Fault);
  RCR_EXPECT(negative.snapshot().fault ==
             ActuatorFaultReason::SoftLimitNegative);
  RCR_EXPECT(negative.snapshot().actual_position_rad >=
             config.min_position_rad);
}

RCR_TEST(tracking_error_requires_consecutive_cycles) {
  MockActuatorConfig config{};
  config.velocity_response_per_s = 0.1;
  config.tracking_error_threshold_rad = 0.001;
  config.tracking_error_cycles = 3;
  MockActuatorProfile profile{config};
  make_ready(profile);
  RCR_REQUIRE(profile.start_velocity(1.0).accepted());
  profile.tick(10ms);
  RCR_EXPECT(profile.snapshot().state == ActuatorState::Running);
  profile.tick(10ms);
  RCR_EXPECT(profile.snapshot().state == ActuatorState::Running);
  profile.tick(10ms);
  RCR_EXPECT(profile.snapshot().state == ActuatorState::Fault);
  RCR_EXPECT(profile.snapshot().fault == ActuatorFaultReason::TrackingError);
}

RCR_TEST(persistent_fault_requires_clear_then_safe_reset) {
  MockActuatorProfile profile;
  make_ready(profile);
  profile.inject_fault(ActuatorFaultReason::EncoderFault, true);
  RCR_EXPECT(profile.snapshot().state == ActuatorState::Fault);
  RCR_EXPECT(!profile.start_velocity(1.0).accepted());
  RCR_EXPECT(!profile.reset_fault().accepted());
  profile.clear_injected_blocker();
  advance(profile, 500ms);
  RCR_REQUIRE(profile.reset_fault().accepted());
  const auto snapshot = profile.snapshot();
  RCR_EXPECT(snapshot.state == ActuatorState::Disabled);
  RCR_EXPECT(!snapshot.drive_enabled);
  RCR_EXPECT(!snapshot.homed);
  RCR_EXPECT(snapshot.target_velocity_rad_s == 0.0);
}

RCR_TEST(drive_disable_during_motion_finishes_disabled) {
  MockActuatorProfile profile;
  make_ready(profile);
  RCR_REQUIRE(profile.start_velocity(1.0).accepted());
  advance(profile, 200ms);
  RCR_REQUIRE(profile.drive_disable().accepted());
  advance(profile, 500ms);
  RCR_EXPECT(profile.snapshot().state == ActuatorState::Disabled);
  RCR_EXPECT(profile.snapshot().actual_velocity_rad_s == 0.0);
}

RCR_TEST_MAIN()
