// Runtime Core：组合状态机、watchdog、mailbox 与周期监督，不直接拥有 Linux fd。
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
  // timeout 在构造 watchdog 时保存，但仍在真正创建线程前校验，使配置错误以 Result
  // 返回，而不是让一个永远立即过期的监督器进入运行状态。
  if (config_.command_timeout.count() <= 0) {
    return Error{Errc::InvalidArgument, "command watchdog timeout must be positive"};
  }
  return scheduler_.start([this](const SchedulerTick& tick) { on_tick(tick); });
}

void LinuxRuntime::stop() {
  // 必须先停止并 join worker，再取得 state_mutex_。若反过来持锁等待 join，而 worker
  // 正在 on_tick 中等待同一把锁，会形成确定性死锁。
  scheduler_.request_stop();
  scheduler_.join();
  std::lock_guard lock(state_mutex_);
  clear_output_path_locked();
  // 用新的默认状态机整体复位，避免未来新增内部锁存字段时 stop 漏清某个状态。
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
  // 取时失败不阻止纯状态事件，但 trace 使用 0 明确表示“没有有效时间戳”。状态迁移本身
  // 仍在 state_mutex_ 下串行，避免 Application 与周期 timeout 同时改变模式。
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
      // 先清除上次 session/sequence/mailbox，再从当前单调时间 arm。这样进入 Active
      // 后即使一条新命令都没收到，也会在 command_timeout 后自动转 Hold。
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
  // 联锁信息和由此产生的状态迁移在同一锁区间完成；不能先写 ready、稍后再清 mailbox，
  // 否则消费线程可能在两步之间取走最后一条输出。
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

void LinuxRuntime::set_fault(FaultCode code) {
  std::lock_guard lock(state_mutex_);
  state_machine_.set_fault(code);
}

void LinuxRuntime::set_supervision_hook(RuntimeSupervisionHook hook) {
  // 与 start 串行由 Application 保证；不在运行中热切换。
  supervision_hook_ = std::move(hook);
}

Result<void> LinuxRuntime::publish_output_command(const OutputCommand& command) {
  // deadline 与当前时间必须来自同一个 CLOCK_MONOTONIC 域。先取时再加锁可缩短临界区；
  // 锁等待只会让命令更接近过期，不会把过期命令错误判断为新鲜。
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
    // 第一条合法命令定义本次 Active 的应用会话；绑定必须晚于所有基本合法性检查，
    // 否则一条 mask=0 或已过期的坏命令会占住 session，拒绝后续正常命令。
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
  // 发布时检查 deadline 仍不足以保证消费时新鲜：latest-wins 槽位可能因 I/O 忙而等待，
  // 所以在真正交给输出路径前必须重新取单调时间并复查。
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
  // 先在状态锁下取得逻辑一致的 mode/fault/interlock，再读取各组件的原子诊断值。
  // 不同时持有所有子组件锁，避免一个只读诊断接口扩大控制路径的锁竞争。
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

  // watchdog check 与 publish/kick 共用 state_mutex_。虽然 watchdog 字段是 atomic，
  // 这里仍需要组合级串行化，保证“检查到超时并清输出”和“接受新命令并 kick”有唯一顺序。
  {
    std::lock_guard lock(state_mutex_);
    if (state_machine_.can_accept_output()) {
      const WatchdogCheck check = command_watchdog_.check(tick.actual_ns);
      if (check.newly_expired) {
        trace_.record(
            TraceEvent{tick.actual_ns, TraceKind::WatchdogExpired, check.age_ns, 0});
        const TransitionResult transition =
            state_machine_.handle(RuntimeEvent::CommandTimeout);
        clear_output_path_locked();
        trace_transition(transition, tick.actual_ns);
      }
    }
  }

  // 监督钩子在锁外运行，以便 NodeSupervisor 回调 handle/set_fault 时不会自死锁。
  if (supervision_hook_) {
    supervision_hook_(tick.actual_ns);
  }
}

void LinuxRuntime::clear_output_path_locked() {
  // 调用者必须已持有 state_mutex_。顺序先 disarm 再清数据，防止未来无锁观察者在清理
  // 过程中把旧 last_kick 当成仍受监督的活动会话；clear 不重置历史诊断计数。
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
