// Orange Pi/Linux Runtime 实现；不得加入 MCU HAL、FreeRTOS 或 ESP-IDF 依赖。
#include "rcr/runtime.hpp"

#include "rcr/time.hpp"

#include <stdexcept>
#include <utility>

namespace rcr {

LinuxRuntime::LinuxRuntime(RuntimeConfig config)
    : config_(config),
      command_watchdog_(config.command_timeout),
      trace_(config.trace_capacity),
      scheduler_(config.scheduler),
      test_throw_on_tick_(config.test_throw_on_tick) {}

LinuxRuntime::~LinuxRuntime() { stop(); }

Result<void> LinuxRuntime::start() {
  if (config_.command_timeout.count() <= 0) {
    return Error{Errc::InvalidArgument, "command watchdog timeout must be positive"};
  }
  return scheduler_.start([this](const SchedulerTick& tick) { on_tick(tick); });
}

void LinuxRuntime::stop() {
  scheduler_.request_stop();
  scheduler_.join();
  std::lock_guard lock(state_mutex_);
  clear_output_path_locked();
  const RuntimeMode before = state_machine_.mode();
  state_machine_ = RuntimeStateMachine{};
  if (before != RuntimeMode::Disabled) {
    const auto now = monotonic_now_ns();
    trace_.record(TraceEvent{now ? now.value() : 0, TraceKind::StateTransition,
                             static_cast<std::int64_t>(before),
                             static_cast<std::int64_t>(RuntimeMode::Disabled)});
  }
}

TransitionResult LinuxRuntime::handle(RuntimeEvent event) {
  const auto now = monotonic_now_ns();
  std::lock_guard lock(state_mutex_);
  if (event == RuntimeEvent::ActivateRequest && !scheduler_.running()) {
    // 没有监督周期时不能进入 Active，否则命令 watchdog 无人检查。
    return TransitionResult{false, state_machine_.mode(), state_machine_.mode(),
                            "runtime scheduler is not running"};
  }
  const TransitionResult transition = state_machine_.handle(event);
  if (transition.accepted) {
    if (transition.to == RuntimeMode::Active && transition.from != RuntimeMode::Active) {
      // 每次激活都建立新的输出会话；绝不复用上次激活遗留的命令。
      clear_output_path_locked();
      command_watchdog_.arm(now ? now.value() : 0);
    } else if (transition.from == RuntimeMode::Active &&
               transition.to != RuntimeMode::Active) {
      clear_output_path_locked();
    }
    trace_transition(transition, now ? now.value() : 0);
  }
  return transition;
}

void LinuxRuntime::set_interlock_ready(bool ready) {
  const auto now = monotonic_now_ns();
  std::lock_guard lock(state_mutex_);
  const RuntimeMode before = state_machine_.mode();
  state_machine_.set_interlock_ready(ready);
  const RuntimeMode after = state_machine_.mode();
  if (before == RuntimeMode::Active && after != RuntimeMode::Active) {
    clear_output_path_locked();
  }
  if (before != after) {
    trace_.record(TraceEvent{now ? now.value() : 0, TraceKind::StateTransition,
                             static_cast<std::int64_t>(before),
                             static_cast<std::int64_t>(after)});
  }
}

Result<void> LinuxRuntime::publish_output_command(const OutputCommand& command) {
  const auto now = monotonic_now_ns();
  if (!now) {
    return now.error();
  }
  std::lock_guard lock(state_mutex_);
  if (!scheduler_.running()) {
    return Error{Errc::Rejected, "runtime scheduler is not running"};
  }
  const auto reject = [this, &command, now_ns = now.value()](const char* message) {
    trace_.record(TraceEvent{now_ns, TraceKind::OutputCommandRejected,
                             static_cast<std::int64_t>(command.sequence), 0});
    return Result<void>{Error{Errc::Rejected, message}};
  };
  if (!state_machine_.can_accept_output()) {
    return reject("runtime is not active or software interlock is open");
  }
  if (command.session_id == 0 || command.sequence == 0 || command.mask == 0) {
    return reject("output command requires nonzero session, sequence, and mask");
  }
  if (command.deadline_ns <= now.value()) {
    return reject("output command deadline has expired or is missing");
  }
  if (!active_session_id_.has_value()) {
    active_session_id_ = command.session_id;
  } else if (*active_session_id_ != command.session_id) {
    return reject("output command belongs to a different active session");
  }
  if (command.sequence <= last_output_sequence_) {
    return reject("output command sequence is stale or duplicated");
  }

  // 状态检查、序号提交、邮箱发布和 watchdog kick 位于同一状态锁区间，避免 Hold 后
  // 仍有命令从竞态窗口穿过软件门控。
  last_output_sequence_ = command.sequence;
  mailbox_.publish(command);
  command_watchdog_.kick(now.value());
  trace_.record(TraceEvent{now.value(), TraceKind::OutputCommandPublished,
                           static_cast<std::int64_t>(command.sequence), command.deadline_ns});
  return Result<void>::success();
}

std::optional<OutputCommand> LinuxRuntime::try_consume_output_command() {
  const auto now = monotonic_now_ns();
  if (!now) {
    return std::nullopt;
  }
  std::lock_guard lock(state_mutex_);
  if (!scheduler_.running() || !state_machine_.can_accept_output()) {
    // 周期线程异常退出时也要关闭消费端，不能只依赖发布端拒绝新命令。
    mailbox_.clear();
    return std::nullopt;
  }
  auto command = mailbox_.try_consume();
  if (command.has_value() && command->deadline_ns <= now.value()) {
    trace_.record(TraceEvent{now.value(), TraceKind::OutputCommandRejected,
                             static_cast<std::int64_t>(command->sequence),
                             command->deadline_ns});
    return std::nullopt;
  }
  return command;
}

RuntimeSnapshot LinuxRuntime::snapshot() const {
  RuntimeSnapshot value{};
  {
    std::lock_guard lock(state_mutex_);
    value.mode = state_machine_.mode();
    value.fault = state_machine_.fault();
    value.interlock_ready = state_machine_.interlock_ready();
  }
  value.running = scheduler_.running();
  value.scheduler = scheduler_.stats();
  value.published_commands = mailbox_.publish_count();
  value.overwritten_commands = mailbox_.drop_count();
  value.trace_dropped = trace_.dropped();
  return value;
}

std::vector<TraceEvent> LinuxRuntime::trace_snapshot() const { return trace_.snapshot(); }

void LinuxRuntime::on_tick(const SchedulerTick& tick) {
  // 测试缝只允许抛一次；生产路径默认关闭，异常由 scheduler 捕获并 fail-closed。
  if (test_throw_on_tick_.exchange(false, std::memory_order_acq_rel)) {
    throw std::runtime_error("test-induced runtime tick failure");
  }

  trace_.record(TraceEvent{tick.actual_ns, TraceKind::SchedulerTick,
                           tick.wakeup_lateness_ns, static_cast<std::int64_t>(tick.sequence)});
  if (tick.wakeup_lateness_ns >= config_.scheduler.period.count()) {
    trace_.record(TraceEvent{tick.actual_ns, TraceKind::DeadlineMiss,
                             tick.wakeup_lateness_ns,
                             static_cast<std::int64_t>(tick.sequence)});
  }

  std::lock_guard lock(state_mutex_);
  if (!state_machine_.can_accept_output()) {
    return;
  }
  const WatchdogCheck check = command_watchdog_.check(tick.actual_ns);
  if (!check.newly_expired) {
    return;
  }

  trace_.record(TraceEvent{tick.actual_ns, TraceKind::WatchdogExpired, check.age_ns, 0});
  const TransitionResult transition = state_machine_.handle(RuntimeEvent::CommandTimeout);
  clear_output_path_locked();
  trace_transition(transition, tick.actual_ns);
}

void LinuxRuntime::clear_output_path_locked() {
  command_watchdog_.disarm();
  mailbox_.clear();
  active_session_id_.reset();
  last_output_sequence_ = 0;
}

void LinuxRuntime::trace_transition(const TransitionResult& transition, std::int64_t now_ns) {
  trace_.record(TraceEvent{now_ns, TraceKind::StateTransition,
                           static_cast<std::int64_t>(transition.from),
                           static_cast<std::int64_t>(transition.to)});
}

}  // namespace rcr
