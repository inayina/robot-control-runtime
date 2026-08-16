// Linux 目标测试：有界输入队列与 NodeSupervisor。
#include "rcr/runtime.hpp"
#include "rcr/runtime_events.hpp"
#include "rcr/time.hpp"
#include "test_support.hpp"

#include <chrono>
#include <thread>

RCR_TEST(BoundedQueueRejectsOverflowWithoutOverwrite) {
  rcr::BoundedInputQueue queue{2};
  rcr::RuntimeInputEvent a{};
  a.kind = rcr::RuntimeInputKind::Heartbeat;
  a.hb_seq = 1;
  rcr::RuntimeInputEvent b = a;
  b.hb_seq = 2;
  rcr::RuntimeInputEvent c = a;
  c.hb_seq = 3;

  RCR_REQUIRE(queue.try_push(a));
  RCR_REQUIRE(queue.try_push(b));
  RCR_EXPECT(!queue.try_push(c));
  RCR_EXPECT(queue.overflow_latched());
  RCR_EXPECT(queue.overflow_count() == 1);
  RCR_EXPECT(queue.size() == 2);

  auto first = queue.try_pop();
  RCR_REQUIRE(first.has_value());
  RCR_EXPECT(first->hb_seq == 1);
  auto second = queue.try_pop();
  RCR_REQUIRE(second.has_value());
  RCR_EXPECT(second->hb_seq == 2);
  RCR_EXPECT(!queue.try_pop().has_value());
}

RCR_TEST(NodeSupervisorDetectsHeartbeatTimeout) {
  rcr::BoundedInputQueue queue{16};
  rcr::NodeSupervisorConfig cfg{};
  cfg.node_id = 1;
  cfg.heartbeat_timeout = std::chrono::milliseconds{50};
  cfg.max_events_per_tick = 8;
  rcr::NodeSupervisor supervisor{cfg, queue};

  rcr::RuntimeConfig runtime_cfg{};
  runtime_cfg.scheduler.period = std::chrono::milliseconds{10};
  rcr::LinuxRuntime runtime{runtime_cfg};
  RCR_REQUIRE(runtime.start().ok());
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::Boot).accepted);
  runtime.set_interlock_ready(true);
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::ActivateRequest).accepted);

  const auto now = rcr::monotonic_now_ns();
  RCR_REQUIRE(now.ok());
  rcr::RuntimeInputEvent hb{};
  hb.kind = rcr::RuntimeInputKind::Heartbeat;
  hb.node_id = 1;
  hb.boot_id = 1;
  hb.session_id = 1;
  hb.hb_seq = 1;
  hb.monotonic_ns = now.value();
  RCR_REQUIRE(queue.try_push(hb));

  supervisor.on_tick(runtime, now.value());
  RCR_EXPECT(supervisor.snapshot().online);

  // 模拟超时：用远大于 timeout 的 now。
  const std::int64_t later = now.value() + 80'000'000LL;
  supervisor.on_tick(runtime, later);
  const auto snap = supervisor.snapshot();
  RCR_EXPECT(!snap.online);
  RCR_EXPECT(snap.comm_loss_latched);
  RCR_EXPECT(runtime.snapshot().mode == rcr::RuntimeMode::Fault);
  RCR_EXPECT(runtime.snapshot().fault == rcr::FaultCode::CommLoss);
  runtime.stop();
}

RCR_TEST(NodeSupervisorLatchesRestartOnBootChange) {
  rcr::BoundedInputQueue queue{16};
  rcr::NodeSupervisorConfig cfg{};
  cfg.node_id = 1;
  cfg.heartbeat_timeout = std::chrono::milliseconds{300};
  rcr::NodeSupervisor supervisor{cfg, queue};

  rcr::LinuxRuntime runtime{};
  RCR_REQUIRE(runtime.start().ok());
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::Boot).accepted);
  runtime.set_interlock_ready(true);
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::ActivateRequest).accepted);

  const auto now = rcr::monotonic_now_ns().value();
  rcr::RuntimeInputEvent hb1{};
  hb1.kind = rcr::RuntimeInputKind::Heartbeat;
  hb1.node_id = 1;
  hb1.boot_id = 1;
  hb1.session_id = 1;
  hb1.hb_seq = 1;
  hb1.monotonic_ns = now;
  RCR_REQUIRE(queue.try_push(hb1));
  supervisor.on_tick(runtime, now);
  RCR_EXPECT(runtime.snapshot().mode == rcr::RuntimeMode::Active);

  rcr::RuntimeInputEvent hb2 = hb1;
  hb2.boot_id = 2;
  hb2.session_id = 2;
  hb2.hb_seq = 0;
  RCR_REQUIRE(queue.try_push(hb2));
  supervisor.on_tick(runtime, now + 1);
  RCR_EXPECT(supervisor.snapshot().restart_latched);
  RCR_EXPECT(runtime.snapshot().mode == rcr::RuntimeMode::Fault);
  runtime.stop();
}

RCR_TEST(NodeSupervisorOverflowForcesFault) {
  rcr::BoundedInputQueue queue{1};
  rcr::NodeSupervisorConfig cfg{};
  cfg.node_id = 1;
  rcr::NodeSupervisor supervisor{cfg, queue};

  rcr::LinuxRuntime runtime{};
  RCR_REQUIRE(runtime.start().ok());
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::Boot).accepted);
  runtime.set_interlock_ready(true);
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::ActivateRequest).accepted);

  rcr::RuntimeInputEvent e{};
  e.kind = rcr::RuntimeInputKind::Heartbeat;
  e.node_id = 1;
  e.boot_id = 1;
  e.session_id = 1;
  e.hb_seq = 1;
  e.monotonic_ns = 1;
  RCR_REQUIRE(queue.try_push(e));
  RCR_EXPECT(!queue.try_push(e));

  supervisor.on_tick(runtime, 1);
  RCR_EXPECT(supervisor.snapshot().overflow_fault_latched);
  RCR_EXPECT(runtime.snapshot().mode == rcr::RuntimeMode::Fault);
  RCR_EXPECT(runtime.snapshot().fault == rcr::FaultCode::Internal);
  runtime.stop();
}

RCR_TEST(NodeSupervisorHonorsPerTickBudget) {
  rcr::BoundedInputQueue queue{8};
  rcr::NodeSupervisorConfig cfg{};
  cfg.node_id = 1;
  cfg.max_events_per_tick = 2;
  rcr::NodeSupervisor supervisor{cfg, queue};
  rcr::LinuxRuntime runtime{};
  RCR_REQUIRE(runtime.start().ok());

  for (std::uint16_t i = 1; i <= 5; ++i) {
    rcr::RuntimeInputEvent e{};
    e.kind = rcr::RuntimeInputKind::Heartbeat;
    e.node_id = 1;
    e.boot_id = 1;
    e.session_id = 1;
    e.hb_seq = i;
    e.monotonic_ns = i;
    RCR_REQUIRE(queue.try_push(e));
  }
  supervisor.on_tick(runtime, 10);
  RCR_EXPECT(supervisor.snapshot().events_processed == 2);
  RCR_EXPECT(queue.size() == 3);
  runtime.stop();
}

RCR_TEST(NodeSupervisorClosesMatchingOutputAck) {
  rcr::BoundedInputQueue queue{8};
  rcr::NodeSupervisor supervisor{{}, queue};
  rcr::RuntimeConfig config{};
  config.command_timeout = std::chrono::seconds{1};
  config.output_ack_timeout = std::chrono::seconds{1};
  rcr::LinuxRuntime runtime{config};
  RCR_REQUIRE(runtime.start().ok());
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::Boot).accepted);
  runtime.set_interlock_ready(true);
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::ActivateRequest).accepted);

  const auto now = rcr::monotonic_now_ns();
  RCR_REQUIRE(now.ok());
  rcr::OutputCommand command{};
  command.session_id = 23;
  command.sequence = 1;
  command.deadline_ns = now.value() + 500'000'000LL;
  command.mask = 1;
  command.values = 1;
  RCR_REQUIRE(runtime.publish_output_command(command).ok());
  RCR_REQUIRE(runtime.try_consume_output_command().has_value());
  RCR_REQUIRE(runtime.note_output_command_sent(23, 1, now.value()).ok());

  rcr::RuntimeInputEvent ack{};
  ack.kind = rcr::RuntimeInputKind::OutputStatus;
  ack.node_id = 1;
  ack.session_id = 23;
  ack.output_sequence = 1;
  ack.output_result = rcr::can_v1::OutputResult::Applied;
  ack.monotonic_ns = now.value() + 1;
  RCR_REQUIRE(queue.try_push(ack));
  supervisor.on_tick(runtime, ack.monotonic_ns);

  const auto snap = runtime.snapshot();
  RCR_EXPECT(!snap.output_ack_pending);
  RCR_EXPECT(snap.last_ack_sequence == 1);
  RCR_EXPECT(snap.last_ack_result == rcr::can_v1::OutputResult::Applied);
  runtime.stop();
}

RCR_TEST(NodeSupervisorRejectsClearWhileCommLossPersists) {
  rcr::BoundedInputQueue queue{8};
  rcr::NodeSupervisorConfig cfg{};
  cfg.node_id = 1;
  cfg.heartbeat_timeout = std::chrono::milliseconds{20};
  rcr::NodeSupervisor supervisor{cfg, queue};
  rcr::LinuxRuntime runtime{};
  RCR_REQUIRE(runtime.start().ok());
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::Boot).accepted);

  rcr::RuntimeInputEvent hb{};
  hb.kind = rcr::RuntimeInputKind::Heartbeat;
  hb.node_id = 1;
  hb.boot_id = 1;
  hb.session_id = 1;
  hb.hb_seq = 1;
  hb.monotonic_ns = 100;
  RCR_REQUIRE(queue.try_push(hb));
  supervisor.on_tick(runtime, 100);
  supervisor.on_tick(runtime, 30'000'100);
  RCR_EXPECT(
      !supervisor.acknowledge_fault_clear(rcr::FaultCode::CommLoss).ok());

  hb.hb_seq = 2;
  hb.monotonic_ns = 30'000'200;
  RCR_REQUIRE(queue.try_push(hb));
  supervisor.on_tick(runtime, hb.monotonic_ns);
  RCR_EXPECT(supervisor.acknowledge_fault_clear(rcr::FaultCode::CommLoss).ok());
  runtime.stop();
}

RCR_TEST(NodeSupervisorRejectsOverflowClearUntilRestart) {
  rcr::BoundedInputQueue queue{1};
  rcr::NodeSupervisor supervisor{{}, queue};
  rcr::LinuxRuntime runtime{};
  RCR_REQUIRE(runtime.start().ok());
  rcr::RuntimeInputEvent event{};
  RCR_REQUIRE(queue.try_push(event));
  RCR_EXPECT(!queue.try_push(event));
  supervisor.on_tick(runtime, 1);
  RCR_EXPECT(
      !supervisor.acknowledge_fault_clear(rcr::FaultCode::Internal).ok());
  runtime.stop();
}

RCR_TEST(OverflowThenCommLossCannotBypassRestartRequirement) {
  rcr::BoundedInputQueue queue{1};
  rcr::NodeSupervisorConfig cfg{};
  cfg.heartbeat_timeout = std::chrono::milliseconds{20};
  rcr::NodeSupervisor supervisor{cfg, queue};
  rcr::LinuxRuntime runtime{};
  RCR_REQUIRE(runtime.start().ok());
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::Boot).accepted);
  runtime.set_interlock_ready(true);
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::ActivateRequest).accepted);

  rcr::RuntimeInputEvent hb{};
  hb.kind = rcr::RuntimeInputKind::Heartbeat;
  hb.node_id = 1;
  hb.boot_id = 1;
  hb.session_id = 1;
  hb.hb_seq = 1;
  hb.monotonic_ns = 100;
  RCR_REQUIRE(queue.try_push(hb));
  RCR_EXPECT(!queue.try_push(hb));
  supervisor.on_tick(runtime, 100);
  RCR_REQUIRE(runtime.snapshot().fault == rcr::FaultCode::Internal);

  supervisor.on_tick(runtime, 30'000'100);
  RCR_REQUIRE(runtime.snapshot().fault == rcr::FaultCode::CommLoss);

  hb.hb_seq = 2;
  hb.monotonic_ns = 30'000'200;
  RCR_REQUIRE(queue.try_push(hb));
  supervisor.on_tick(runtime, hb.monotonic_ns);
  RCR_REQUIRE(!supervisor.snapshot().comm_loss_latched);
  RCR_EXPECT(
      !supervisor.acknowledge_fault_clear(rcr::FaultCode::CommLoss).ok());
  RCR_EXPECT(runtime.snapshot().mode == rcr::RuntimeMode::Fault);
  runtime.stop();
}

RCR_TEST(NodeFaultThenCommLossCannotClearPersistentNodeFault) {
  rcr::BoundedInputQueue queue{8};
  rcr::NodeSupervisorConfig cfg{};
  cfg.heartbeat_timeout = std::chrono::milliseconds{20};
  rcr::NodeSupervisor supervisor{cfg, queue};
  rcr::LinuxRuntime runtime{};
  RCR_REQUIRE(runtime.start().ok());
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::Boot).accepted);

  rcr::RuntimeInputEvent hb{};
  hb.kind = rcr::RuntimeInputKind::Heartbeat;
  hb.node_id = 1;
  hb.boot_id = 1;
  hb.session_id = 1;
  hb.hb_seq = 1;
  hb.monotonic_ns = 100;
  RCR_REQUIRE(queue.try_push(hb));

  rcr::RuntimeInputEvent status{};
  status.kind = rcr::RuntimeInputKind::NodeStatus;
  status.node_id = 1;
  status.session_id = 1;
  status.interlock_ready = true;
  status.node_fault_code = 9;
  RCR_REQUIRE(queue.try_push(status));
  supervisor.on_tick(runtime, 100);
  RCR_REQUIRE(runtime.snapshot().fault == rcr::FaultCode::NodeFault);

  supervisor.on_tick(runtime, 30'000'100);
  RCR_REQUIRE(runtime.snapshot().fault == rcr::FaultCode::CommLoss);

  hb.hb_seq = 2;
  hb.monotonic_ns = 30'000'200;
  RCR_REQUIRE(queue.try_push(hb));
  supervisor.on_tick(runtime, hb.monotonic_ns);
  RCR_REQUIRE(!supervisor.snapshot().comm_loss_latched);
  RCR_EXPECT(
      !supervisor.acknowledge_fault_clear(rcr::FaultCode::CommLoss).ok());
  RCR_EXPECT(supervisor.snapshot().node_fault_code == 9);
  runtime.stop();
}

RCR_TEST(AllRecoverableBlockersClearedReturnsOnlyIdle) {
  rcr::BoundedInputQueue queue{8};
  rcr::NodeSupervisor supervisor{{}, queue};
  rcr::LinuxRuntime runtime{};
  RCR_REQUIRE(runtime.start().ok());
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::Boot).accepted);

  rcr::RuntimeInputEvent hb{};
  hb.kind = rcr::RuntimeInputKind::Heartbeat;
  hb.node_id = 1;
  hb.boot_id = 1;
  hb.session_id = 1;
  hb.monotonic_ns = 100;
  RCR_REQUIRE(queue.try_push(hb));

  rcr::RuntimeInputEvent status{};
  status.kind = rcr::RuntimeInputKind::NodeStatus;
  status.node_id = 1;
  status.session_id = 1;
  status.interlock_ready = true;
  status.node_fault_code = 9;
  RCR_REQUIRE(queue.try_push(status));
  supervisor.on_tick(runtime, 100);
  RCR_REQUIRE(runtime.snapshot().mode == rcr::RuntimeMode::Fault);

  status.node_fault_code = 0;
  RCR_REQUIRE(queue.try_push(status));
  supervisor.on_tick(runtime, 101);
  RCR_REQUIRE(
      supervisor.acknowledge_fault_clear(rcr::FaultCode::NodeFault).ok());
  const auto cleared = runtime.handle(rcr::RuntimeEvent::FaultCleared);
  RCR_EXPECT(cleared.accepted);
  RCR_EXPECT(runtime.snapshot().mode == rcr::RuntimeMode::Idle);
  RCR_EXPECT(runtime.handle(rcr::RuntimeEvent::ActivateRequest).accepted);
  runtime.stop();
}

RCR_TEST(NodeSupervisorRetainsInputBitsWithoutTreatingReachedAsFault) {
  rcr::BoundedInputQueue queue{8};
  rcr::NodeSupervisor supervisor{{}, queue};
  rcr::LinuxRuntime runtime{};
  RCR_REQUIRE(runtime.start().ok());
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::Boot).accepted);
  runtime.set_interlock_ready(true);
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::ActivateRequest).accepted);

  rcr::RuntimeInputEvent hb{};
  hb.kind = rcr::RuntimeInputKind::Heartbeat;
  hb.node_id = 1;
  hb.boot_id = 1;
  hb.session_id = 1;
  hb.hb_seq = 1;
  hb.monotonic_ns = 100;
  RCR_REQUIRE(queue.try_push(hb));

  rcr::RuntimeInputEvent status{};
  status.kind = rcr::RuntimeInputKind::NodeStatus;
  status.node_id = 1;
  status.session_id = 1;
  status.interlock_ready = true;
  status.input_bits = rcr::can_v1::kInputBitPositionReached;
  status.node_fault_code = 0;
  RCR_REQUIRE(queue.try_push(status));
  supervisor.on_tick(runtime, 100);

  const auto snap = supervisor.snapshot();
  RCR_EXPECT(snap.input_bits == rcr::can_v1::kInputBitPositionReached);
  RCR_EXPECT(snap.node_fault_code == 0);
  RCR_EXPECT(runtime.snapshot().mode == rcr::RuntimeMode::Active);
  RCR_EXPECT(runtime.snapshot().fault == rcr::FaultCode::None);
  runtime.stop();
}

RCR_TEST(NodeSupervisorStillFaultsOnNodeFaultCodeWithReachedBits) {
  rcr::BoundedInputQueue queue{8};
  rcr::NodeSupervisor supervisor{{}, queue};
  rcr::LinuxRuntime runtime{};
  RCR_REQUIRE(runtime.start().ok());
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::Boot).accepted);

  rcr::RuntimeInputEvent status{};
  status.kind = rcr::RuntimeInputKind::NodeStatus;
  status.node_id = 1;
  status.session_id = 1;
  status.interlock_ready = true;
  status.input_bits = rcr::can_v1::kInputBitPositionReached;
  status.node_fault_code = 4;
  RCR_REQUIRE(queue.try_push(status));
  supervisor.on_tick(runtime, 100);

  const auto snap = supervisor.snapshot();
  RCR_EXPECT(snap.input_bits == rcr::can_v1::kInputBitPositionReached);
  RCR_EXPECT(snap.node_fault_code == 4);
  RCR_EXPECT(runtime.snapshot().mode == rcr::RuntimeMode::Fault);
  RCR_EXPECT(runtime.snapshot().fault == rcr::FaultCode::NodeFault);
  runtime.stop();
}

RCR_TEST(NodeSupervisorRetainsLastOutputMirrorFromOutputStatus) {
  rcr::BoundedInputQueue queue{8};
  rcr::NodeSupervisor supervisor{{}, queue};
  rcr::LinuxRuntime runtime{};
  RCR_REQUIRE(runtime.start().ok());

  rcr::RuntimeInputEvent ack{};
  ack.kind = rcr::RuntimeInputKind::OutputStatus;
  ack.node_id = 1;
  ack.session_id = 1;
  ack.output_sequence = 1;
  ack.output_result = rcr::can_v1::OutputResult::Applied;
  ack.output_mirror = 0x05;
  ack.monotonic_ns = 100;
  RCR_REQUIRE(queue.try_push(ack));
  supervisor.on_tick(runtime, 100);
  RCR_EXPECT(supervisor.snapshot().last_output_mirror == 0x05);
  runtime.stop();
}

RCR_TEST_MAIN()
