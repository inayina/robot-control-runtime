// Linux 目标测试；不依赖 MCU 工具链或真实 CAN 硬件。
#include "rcr/state_machine.hpp"
#include "test_support.hpp"

using rcr::FaultCode;
using rcr::RuntimeEvent;
using rcr::RuntimeMode;
using rcr::RuntimeStateMachine;

RCR_TEST(BootToIdleThenActivationRequiresInterlock) {
  RuntimeStateMachine sm;
  RCR_EXPECT(sm.mode() == RuntimeMode::Disabled);
  RCR_EXPECT(!sm.can_accept_output());

  RCR_REQUIRE(sm.handle(RuntimeEvent::Boot).accepted);
  RCR_EXPECT(sm.mode() == RuntimeMode::Idle);
  RCR_EXPECT(!sm.handle(RuntimeEvent::ActivateRequest).accepted);

  sm.set_interlock_ready(true);
  RCR_REQUIRE(sm.handle(RuntimeEvent::ActivateRequest).accepted);
  RCR_EXPECT(sm.mode() == RuntimeMode::Active);
  RCR_EXPECT(sm.can_accept_output());
}

RCR_TEST(CommandTimeoutRequiresTwoStepRecovery) {
  RuntimeStateMachine sm;
  sm.set_interlock_ready(true);
  RCR_REQUIRE(sm.handle(RuntimeEvent::Boot).accepted);
  RCR_REQUIRE(sm.handle(RuntimeEvent::ActivateRequest).accepted);

  RCR_REQUIRE(sm.handle(RuntimeEvent::CommandTimeout).accepted);
  RCR_EXPECT(sm.mode() == RuntimeMode::Hold);
  RCR_EXPECT(sm.fault() == FaultCode::Watchdog);
  RCR_EXPECT(!sm.can_accept_output());

  // Resume 只确认 Hold 并回到 Idle；旧输出不会自动恢复。
  RCR_REQUIRE(sm.handle(RuntimeEvent::Resume).accepted);
  RCR_EXPECT(sm.mode() == RuntimeMode::Idle);
  RCR_EXPECT(!sm.can_accept_output());
  RCR_REQUIRE(sm.handle(RuntimeEvent::ActivateRequest).accepted);
  RCR_EXPECT(sm.mode() == RuntimeMode::Active);
}

RCR_TEST(InterlockLossForcesHold) {
  RuntimeStateMachine sm;
  sm.set_interlock_ready(true);
  RCR_REQUIRE(sm.handle(RuntimeEvent::Boot).accepted);
  RCR_REQUIRE(sm.handle(RuntimeEvent::ActivateRequest).accepted);

  sm.set_interlock_ready(false);
  RCR_EXPECT(sm.mode() == RuntimeMode::Hold);
  RCR_EXPECT(sm.fault() == FaultCode::InterlockLost);
  RCR_EXPECT(!sm.can_accept_output());

  sm.set_interlock_ready(true);
  RCR_EXPECT(sm.mode() == RuntimeMode::Hold);
}

RCR_TEST(EStopLatchesUntilExplicitReset) {
  RuntimeStateMachine sm;
  sm.set_interlock_ready(true);
  RCR_REQUIRE(sm.handle(RuntimeEvent::Boot).accepted);
  RCR_REQUIRE(sm.handle(RuntimeEvent::ActivateRequest).accepted);
  RCR_REQUIRE(sm.handle(RuntimeEvent::EStopTrigger).accepted);
  RCR_EXPECT(sm.mode() == RuntimeMode::EStop);

  RCR_EXPECT(!sm.handle(RuntimeEvent::ActivateRequest).accepted);
  RCR_REQUIRE(sm.handle(RuntimeEvent::EStopReset).accepted);
  RCR_EXPECT(sm.mode() == RuntimeMode::Idle);
  RCR_EXPECT(!sm.can_accept_output());
}

RCR_TEST(EStopResetRequiresClosedSoftwareInterlock) {
  RuntimeStateMachine sm;
  RCR_REQUIRE(sm.handle(RuntimeEvent::Boot).accepted);
  RCR_REQUIRE(sm.handle(RuntimeEvent::EStopTrigger).accepted);
  sm.set_interlock_ready(false);

  RCR_EXPECT(!sm.handle(RuntimeEvent::EStopReset).accepted);
  RCR_EXPECT(sm.mode() == RuntimeMode::EStop);
}

RCR_TEST(FaultClearsToIdle) {
  RuntimeStateMachine sm;
  sm.set_interlock_ready(true);
  RCR_REQUIRE(sm.handle(RuntimeEvent::Boot).accepted);
  sm.set_fault(FaultCode::NodeFault);
  RCR_REQUIRE(sm.handle(RuntimeEvent::FaultDetected).accepted);
  RCR_EXPECT(sm.mode() == RuntimeMode::Fault);

  RCR_REQUIRE(sm.handle(RuntimeEvent::FaultCleared).accepted);
  RCR_EXPECT(sm.mode() == RuntimeMode::Idle);
  RCR_EXPECT(sm.fault() == FaultCode::None);
}

RCR_TEST_MAIN()
