// Linux 目标测试；不依赖 MCU 工具链或真实 CAN 硬件。
#include "rcr/runtime.hpp"
#include "rcr/time.hpp"
#include "test_support.hpp"

#include <chrono>
#include <thread>

namespace {

rcr::OutputCommand valid_command(std::uint64_t session, std::uint64_t sequence,
                                 std::int64_t deadline_ns) {
  return rcr::OutputCommand{.session_id = session,
                            .sequence = sequence,
                            .deadline_ns = deadline_ns,
                            .mask = 1,
                            .values = 1};
}

void boot_and_activate(rcr::LinuxRuntime &runtime) {
  RCR_REQUIRE(runtime.start().ok());
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::Boot).accepted);
  runtime.set_interlock_ready(true);
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::ActivateRequest).accepted);
}

} // namespace

RCR_TEST(CommandWatchdogMovesActiveRuntimeToHold) {
  rcr::RuntimeConfig config{};
  config.scheduler.period = std::chrono::milliseconds{1};
  config.command_timeout = std::chrono::milliseconds{8};
  rcr::LinuxRuntime runtime(config);
  boot_and_activate(runtime);

  const auto now = rcr::monotonic_now_ns();
  RCR_REQUIRE(now.ok());
  RCR_REQUIRE(runtime
                  .publish_output_command(
                      valid_command(1, 1, now.value() + 100'000'000LL))
                  .ok());

  bool held = false;
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (runtime.snapshot().mode == rcr::RuntimeMode::Hold) {
      held = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  RCR_EXPECT(held);
  RCR_EXPECT(runtime.snapshot().fault == rcr::FaultCode::Watchdog);
  RCR_EXPECT(!runtime.try_consume_output_command().has_value());

  bool saw_expiration = false;
  for (const auto &event : runtime.trace_snapshot()) {
    saw_expiration =
        saw_expiration || event.kind == rcr::TraceKind::WatchdogExpired;
  }
  RCR_EXPECT(saw_expiration);
  runtime.stop();
  RCR_EXPECT(runtime.snapshot().mode == rcr::RuntimeMode::Disabled);
}

RCR_TEST(CommandsRequireActiveStateAndFreshDeadline) {
  rcr::LinuxRuntime runtime;
  RCR_EXPECT(!runtime.publish_output_command(rcr::OutputCommand{}).ok());
  boot_and_activate(runtime);

  const auto now = rcr::monotonic_now_ns();
  RCR_REQUIRE(now.ok());
  RCR_EXPECT(
      !runtime.publish_output_command(valid_command(1, 1, now.value() - 1))
           .ok());
  RCR_EXPECT(!runtime
                  .publish_output_command(
                      rcr::OutputCommand{.session_id = 1,
                                         .sequence = 1,
                                         .deadline_ns = now.value() + 1'000'000,
                                         .mask = 0})
                  .ok());
  runtime.stop();
}

RCR_TEST(ActivationRequiresRunningSupervisor) {
  rcr::LinuxRuntime runtime;
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::Boot).accepted);
  runtime.set_interlock_ready(true);
  RCR_EXPECT(!runtime.handle(rcr::RuntimeEvent::ActivateRequest).accepted);
  RCR_EXPECT(runtime.snapshot().mode == rcr::RuntimeMode::Idle);

  RCR_REQUIRE(runtime.start().ok());
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::ActivateRequest).accepted);
  runtime.stop();
}

RCR_TEST(SessionAndSequenceRejectReplay) {
  rcr::LinuxRuntime runtime;
  boot_and_activate(runtime);
  const auto now = rcr::monotonic_now_ns();
  RCR_REQUIRE(now.ok());
  const auto deadline = now.value() + 100'000'000LL;

  RCR_REQUIRE(
      runtime.publish_output_command(valid_command(41, 1, deadline)).ok());
  RCR_EXPECT(
      !runtime.publish_output_command(valid_command(41, 1, deadline)).ok());
  RCR_EXPECT(
      !runtime.publish_output_command(valid_command(99, 2, deadline)).ok());
  RCR_REQUIRE(
      runtime.publish_output_command(valid_command(41, 2, deadline)).ok());

  const auto command = runtime.try_consume_output_command();
  RCR_REQUIRE(command.has_value());
  RCR_EXPECT(command->session_id == 41);
  RCR_EXPECT(command->sequence == 2);
  runtime.stop();
}

RCR_TEST(ExpiredOrDeactivatedCommandsNeverReachConsumer) {
  rcr::LinuxRuntime runtime;
  boot_and_activate(runtime);
  auto now = rcr::monotonic_now_ns();
  RCR_REQUIRE(now.ok());
  RCR_REQUIRE(runtime
                  .publish_output_command(
                      valid_command(1, 1, now.value() + 2'000'000LL))
                  .ok());
  std::this_thread::sleep_for(std::chrono::milliseconds{4});
  RCR_EXPECT(!runtime.try_consume_output_command().has_value());

  now = rcr::monotonic_now_ns();
  RCR_REQUIRE(now.ok());
  RCR_REQUIRE(runtime
                  .publish_output_command(
                      valid_command(1, 2, now.value() + 100'000'000LL))
                  .ok());
  RCR_REQUIRE(runtime.handle(rcr::RuntimeEvent::Hold).accepted);
  RCR_EXPECT(!runtime.try_consume_output_command().has_value());
  runtime.stop();
}

RCR_TEST(RaiseFaultAtomicallyClosesOutputTransaction) {
  rcr::RuntimeConfig config{};
  config.command_timeout = std::chrono::seconds{1};
  rcr::LinuxRuntime runtime{config};
  boot_and_activate(runtime);

  const auto now = rcr::monotonic_now_ns();
  RCR_REQUIRE(now.ok());
  RCR_REQUIRE(runtime
                  .publish_output_command(
                      valid_command(7, 1, now.value() + 500'000'000LL))
                  .ok());

  const auto raised = runtime.raise_fault(rcr::FaultCode::CommLoss);
  RCR_REQUIRE(raised.accepted);
  RCR_EXPECT(raised.from == rcr::RuntimeMode::Active);
  RCR_EXPECT(raised.to == rcr::RuntimeMode::Fault);
  const auto snap = runtime.snapshot();
  RCR_EXPECT(snap.mode == rcr::RuntimeMode::Fault);
  RCR_EXPECT(snap.fault == rcr::FaultCode::CommLoss);
  RCR_EXPECT(!snap.output_ack_pending);
  RCR_EXPECT(!runtime.try_consume_output_command().has_value());
  RCR_EXPECT(!runtime
                  .publish_output_command(
                      valid_command(7, 2, now.value() + 500'000'000LL))
                  .ok());
  // 通用入口明确拒绝拆开的第二阶段调用，避免未来调用方重新引入竞态。
  RCR_EXPECT(!runtime.handle(rcr::RuntimeEvent::FaultDetected).accepted);
  bool saw_fault_trace = false;
  for (const auto &event : runtime.trace_snapshot()) {
    saw_fault_trace =
        saw_fault_trace ||
        (event.kind == rcr::TraceKind::FaultRaised &&
         event.value_a == static_cast<std::int64_t>(rcr::FaultCode::CommLoss));
  }
  RCR_EXPECT(saw_fault_trace);
  runtime.stop();
}

RCR_TEST(OutputAckClosesSingleInflightAndReleasesLatestCommand) {
  rcr::RuntimeConfig config{};
  config.command_timeout = std::chrono::seconds{1};
  config.output_ack_timeout = std::chrono::seconds{1};
  rcr::LinuxRuntime runtime{config};
  boot_and_activate(runtime);

  const auto now = rcr::monotonic_now_ns();
  RCR_REQUIRE(now.ok());
  const auto deadline = now.value() + 500'000'000LL;
  RCR_REQUIRE(
      runtime.publish_output_command(valid_command(9, 1, deadline)).ok());
  const auto first = runtime.try_consume_output_command();
  RCR_REQUIRE(first.has_value());
  RCR_REQUIRE(runtime.note_output_command_sent(9, 1, now.value()).ok());

  RCR_REQUIRE(
      runtime.publish_output_command(valid_command(9, 2, deadline)).ok());
  RCR_REQUIRE(
      runtime.publish_output_command(valid_command(9, 3, deadline)).ok());
  RCR_EXPECT(!runtime.try_consume_output_command().has_value());
  runtime.observe_output_status(9, 1, rcr::can_v1::OutputResult::Applied,
                                now.value() + 1);

  const auto snap = runtime.snapshot();
  RCR_EXPECT(!snap.output_ack_pending);
  RCR_EXPECT(snap.last_sent_sequence == 1);
  RCR_EXPECT(snap.last_ack_sequence == 1);
  RCR_EXPECT(snap.last_ack_result == rcr::can_v1::OutputResult::Applied);
  RCR_EXPECT(snap.unexpected_ack_count == 0);
  RCR_EXPECT(snap.overwritten_commands == 1);
  const auto latest = runtime.try_consume_output_command();
  RCR_REQUIRE(latest.has_value());
  RCR_EXPECT(latest->sequence == 3);
  runtime.stop();
}

RCR_TEST(UnexpectedAckStaysPendingUntilAckTimeoutHold) {
  rcr::RuntimeConfig config{};
  config.scheduler.period = std::chrono::milliseconds{1};
  config.command_timeout = std::chrono::seconds{1};
  config.output_ack_timeout = std::chrono::milliseconds{8};
  rcr::LinuxRuntime runtime{config};
  boot_and_activate(runtime);

  const auto now = rcr::monotonic_now_ns();
  RCR_REQUIRE(now.ok());
  RCR_REQUIRE(runtime
                  .publish_output_command(
                      valid_command(11, 1, now.value() + 500'000'000LL))
                  .ok());
  RCR_REQUIRE(runtime.try_consume_output_command().has_value());
  RCR_REQUIRE(runtime.note_output_command_sent(11, 1, now.value()).ok());

  runtime.observe_output_status(
      12, 1, rcr::can_v1::OutputResult::SessionMismatch, now.value() + 1);
  RCR_EXPECT(runtime.snapshot().output_ack_pending);
  RCR_EXPECT(runtime.snapshot().unexpected_ack_count == 1);

  bool held = false;
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (runtime.snapshot().mode == rcr::RuntimeMode::Hold) {
      held = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  RCR_REQUIRE(held);
  const auto snap = runtime.snapshot();
  RCR_EXPECT(snap.ack_timeout_count == 1);
  RCR_EXPECT(snap.fault == rcr::FaultCode::AckTimeout);
  RCR_EXPECT(rcr::to_string(snap.fault) == "ACK_TIMEOUT");
  RCR_EXPECT(!snap.output_ack_pending);
  RCR_EXPECT(snap.last_ack_result ==
             rcr::can_v1::OutputResult::SessionMismatch);
  const auto resumed = runtime.handle(rcr::RuntimeEvent::Resume);
  RCR_EXPECT(resumed.accepted);
  RCR_EXPECT(runtime.snapshot().mode == rcr::RuntimeMode::Idle);
  runtime.stop();
}

RCR_TEST(WorkerFailureClosesPublishAndConsumeWithoutAutoFault) {
  // Core fail-closed：worker 退出后命令路径关闭；应用状态升级留给未来 daemon。
  rcr::RuntimeConfig config{};
  config.scheduler.period = std::chrono::milliseconds{1};
  config.command_timeout = std::chrono::milliseconds{100};
  config.test_throw_on_tick = true;
  rcr::LinuxRuntime runtime(config);
  boot_and_activate(runtime);

  bool worker_stopped = false;
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto snap = runtime.snapshot();
    if (!snap.running && snap.scheduler.worker_error != 0) {
      worker_stopped = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  RCR_REQUIRE(worker_stopped);

  const auto snap = runtime.snapshot();
  RCR_EXPECT(!snap.running);
  RCR_EXPECT(snap.scheduler.worker_error != 0);
  // Core 不在 worker 异常时自动改写应用状态；可见性差异由 daemon 阶段关闭。
  RCR_EXPECT(snap.mode == rcr::RuntimeMode::Active);
  RCR_EXPECT(snap.fault == rcr::FaultCode::None);

  const auto now = rcr::monotonic_now_ns();
  RCR_REQUIRE(now.ok());
  RCR_EXPECT(!runtime
                  .publish_output_command(
                      valid_command(1, 1, now.value() + 100'000'000LL))
                  .ok());
  RCR_EXPECT(!runtime.try_consume_output_command().has_value());
  runtime.stop();
}

RCR_TEST_MAIN()
