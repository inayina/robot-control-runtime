// Runtime Core：基于 CLOCK_MONOTONIC 的周期调度与可观察 FIFO 降级。
#include "rcr/scheduler.hpp"

#include "rcr/time.hpp"

#include <cerrno>
#include <cstring>
#include <limits>
#include <pthread.h>
#include <sched.h>
#include <string>
#include <utility>

namespace rcr {

PeriodicScheduler::PeriodicScheduler(SchedulerConfig config) : config_(config) {}

PeriodicScheduler::~PeriodicScheduler() {
  // 析构必须同时发出停止请求并回收线程。只销毁仍 joinable 的 std::thread 会触发
  // std::terminate，因此 RAII 清理不能只调用 request_stop()。
  request_stop();
  join();
}

Result<void> PeriodicScheduler::start(TickCallback callback) {
  // 在创建线程前完成纯配置校验，避免为确定性输入错误启动/回收一次 worker。
  if (config_.period.count() <= 0) {
    return Error{Errc::InvalidArgument, "scheduler period must be positive"};
  }
  if (config_.fifo_priority < 0 || config_.fifo_priority > 99) {
    return Error{Errc::InvalidArgument, "SCHED_FIFO priority must be 0..99"};
  }
  if (config_.require_fifo && config_.fifo_priority == 0) {
    return Error{Errc::InvalidArgument, "require_fifo needs a non-zero priority"};
  }
  if (config_.cpu_affinity < -1 || config_.cpu_affinity >= CPU_SETSIZE) {
    return Error{Errc::InvalidArgument, "CPU affinity must be -1 or less than CPU_SETSIZE"};
  }
  if (!callback) {
    return Error{Errc::InvalidArgument, "scheduler callback is empty"};
  }
  if (thread_.joinable()) {
    // running()==false 也可能是 worker 已异常退出但尚未 join；只看 running 会覆盖仍需
    // 回收的 thread_。joinable 才是 std::thread 生命周期的权威判断。
    return Error{Errc::Busy, "scheduler already started"};
  }

  // 同一对象允许 stop/join 后再次启动，因此每次 start 都重置停止标志、启动握手和统计。
  // 诊断统计彼此独立，不要求形成事务快照，使用 relaxed 足够。
  stop_requested_.store(false, std::memory_order_release);
  running_.store(false, std::memory_order_release);
  cycles_.store(0, std::memory_order_relaxed);
  deadline_misses_.store(0, std::memory_order_relaxed);
  min_lateness_ns_.store(0, std::memory_order_relaxed);
  max_lateness_ns_.store(0, std::memory_order_relaxed);
  total_lateness_ns_.store(0, std::memory_order_relaxed);
  fifo_enabled_.store(false, std::memory_order_relaxed);
  fifo_error_.store(0, std::memory_order_relaxed);
  affinity_enabled_.store(false, std::memory_order_relaxed);
  affinity_error_.store(0, std::memory_order_relaxed);
  worker_error_.store(0, std::memory_order_relaxed);
  {
    std::lock_guard lock(startup_mutex_);
    startup_done_ = false;
    startup_error_ = Error{};
  }

  try {
    // callback 按值移动到 worker，避免 start 返回后继续依赖调用方临时函数对象的生命周期。
    thread_ = std::thread(&PeriodicScheduler::run, this, std::move(callback));
  } catch (const std::system_error& error) {
    return Error{Errc::IoError, std::string("create scheduler thread: ") + error.what()};
  }

  // 条件变量 wait 会在睡眠时释放 mutex，worker 才能写 startup_error_；醒来后重新加锁。
  // 谓词处理虚假唤醒，保证只有 startup_done_ 才读取结果。
  std::unique_lock lock(startup_mutex_);
  startup_cv_.wait(lock, [this] { return startup_done_; });
  const Error startup_error = startup_error_;
  lock.unlock();
  if (startup_error) {
    // worker 在启动阶段已退出，但 std::thread 仍是 joinable；返回错误前必须回收。
    join();
    return startup_error;
  }
  return Result<void>::success();
}

void PeriodicScheduler::request_stop() noexcept {
  // release 发布停止意图；worker 循环用 acquire 观察。此操作不唤醒绝对睡眠，因而不会
  // 承诺“立即停止”，只承诺在当前等待边界后收敛。
  stop_requested_.store(true, std::memory_order_release);
}

void PeriodicScheduler::join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool PeriodicScheduler::running() const noexcept {
  return running_.load(std::memory_order_acquire);
}

SchedulerStats PeriodicScheduler::stats() const noexcept {
  // 这是无锁诊断快照：cycles 与 lateness 可能来自相邻时刻。它适合日志/benchmark，
  // 不适合据此做需要强一致性的状态迁移。
  SchedulerStats value{};
  value.cycles = cycles_.load(std::memory_order_relaxed);
  value.deadline_misses = deadline_misses_.load(std::memory_order_relaxed);
  value.min_lateness_ns = min_lateness_ns_.load(std::memory_order_relaxed);
  value.max_lateness_ns = max_lateness_ns_.load(std::memory_order_relaxed);
  const std::int64_t total = total_lateness_ns_.load(std::memory_order_relaxed);
  if (value.cycles != 0) {
    value.mean_lateness_ns = total / static_cast<std::int64_t>(value.cycles);
  }
  value.fifo_enabled = fifo_enabled_.load(std::memory_order_relaxed);
  value.fifo_error = fifo_error_.load(std::memory_order_relaxed);
  value.affinity_enabled = affinity_enabled_.load(std::memory_order_relaxed);
  value.affinity_error = affinity_error_.load(std::memory_order_relaxed);
  value.worker_error = worker_error_.load(std::memory_order_relaxed);
  return value;
}

const SchedulerConfig& PeriodicScheduler::config() const noexcept { return config_; }

void PeriodicScheduler::run(TickCallback callback) {
  // affinity 和 SCHED_FIFO 都是每线程属性，必须在 worker 内申请并通过启动握手
  // 把真实结果交回调用方；只记录命令行请求值会把配置误报为已生效。
  // pthread API 直接返回错误号而不是通过 errno 返回，strerror 必须使用 rc。
  Error startup_error{};
  if (config_.cpu_affinity >= 0) {
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(config_.cpu_affinity, &cpu_set);
    const int rc = ::pthread_setaffinity_np(::pthread_self(), sizeof(cpu_set), &cpu_set);
    if (rc == 0) {
      affinity_enabled_.store(true, std::memory_order_relaxed);
    } else {
      affinity_error_.store(rc, std::memory_order_relaxed);
      const Errc code = rc == EINVAL ? Errc::InvalidArgument
                                     : ((rc == EPERM || rc == EACCES) ? Errc::Rejected
                                                                      : Errc::IoError);
      startup_error = Error{code, std::string("pthread_setaffinity_np: ") + std::strerror(rc)};
    }
  }
  if (!startup_error && config_.fifo_priority > 0) {
    sched_param params{};
    params.sched_priority = config_.fifo_priority;
    const int rc = ::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &params);
    if (rc == 0) {
      fifo_enabled_.store(true, std::memory_order_relaxed);
    } else {
      fifo_error_.store(rc, std::memory_order_relaxed);
      if (config_.require_fifo) {
        startup_error = Error{Errc::Rejected,
                              std::string("pthread_setschedparam(SCHED_FIFO): ") +
                                  std::strerror(rc)};
      }
    }
  }

  // 第一个期限以启动完成时的 CLOCK_MONOTONIC 为基准。若取时失败，就没有可靠的
  // 绝对时间域，不能退化成墙钟或相对 sleep 后继续假装周期语义成立。
  auto now_result = monotonic_now_ns();
  if (!now_result && !startup_error) {
    startup_error = now_result.error();
  }

  {
    // 在同一互斥区间发布 error、done 和 running，start() 被唤醒后能看到完整启动结果。
    std::lock_guard lock(startup_mutex_);
    startup_error_ = startup_error;
    startup_done_ = true;
    running_.store(!startup_error, std::memory_order_release);
  }
  startup_cv_.notify_one();
  if (startup_error) {
    return;
  }

  const std::int64_t period_ns = config_.period.count();
  // next_ns 始终表示“下一次计划边界”，而不是“上一次实际醒来时间 + period”。
  // 这是避免 callback 和调度延迟长期累积的关键不变量。
  std::int64_t next_ns = now_result.value() + period_ns;
  std::uint64_t sequence = 0;

  while (!stop_requested_.load(std::memory_order_acquire)) {
    const timespec deadline = ns_to_timespec(next_ns);
    int sleep_rc = 0;
    do {
      // clock_nanosleep 返回错误号本身；信号打断时，绝对 deadline 无需像相对 sleep
      // 那样计算剩余时间，直接用同一 deadline 重试即可。
      sleep_rc = ::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr);
    } while (sleep_rc == EINTR && !stop_requested_.load(std::memory_order_acquire));
    if (stop_requested_.load(std::memory_order_acquire)) {
      break;
    }
    if (sleep_rc != 0) {
      // 时钟等待失败后无法保证周期语义，停止线程比退化为相对 sleep 更可诊断。
      worker_error_.store(sleep_rc, std::memory_order_relaxed);
      break;
    }

    auto actual_result = monotonic_now_ns();
    if (!actual_result) {
      worker_error_.store(EIO, std::memory_order_relaxed);
      break;
    }
    const std::int64_t actual_ns = actual_result.value();
    const std::int64_t lateness_ns = actual_ns > next_ns ? actual_ns - next_ns : 0;
    // min/max 用 CAS 更新是为了允许观察线程无锁读取。CAS 失败表示值被并发更新，
    // compare_exchange_weak 会把 current_* 改成最新值，循环再判断是否仍需写入。
    const std::uint64_t cycle = cycles_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (cycle == 1) {
      min_lateness_ns_.store(lateness_ns, std::memory_order_relaxed);
    } else {
      std::int64_t current_min = min_lateness_ns_.load(std::memory_order_relaxed);
      while (lateness_ns < current_min &&
             !min_lateness_ns_.compare_exchange_weak(current_min, lateness_ns,
                                                     std::memory_order_relaxed)) {
      }
    }
    std::int64_t current_max = max_lateness_ns_.load(std::memory_order_relaxed);
    while (lateness_ns > current_max &&
           !max_lateness_ns_.compare_exchange_weak(current_max, lateness_ns,
                                                   std::memory_order_relaxed)) {
    }
    total_lateness_ns_.fetch_add(lateness_ns, std::memory_order_relaxed);

    try {
      // callback 与 worker 同线程串行执行；这里是异常边界。异常越过 std::thread 入口会
      // 调用 std::terminate，所以转换为 worker_error 并结束周期路径。
      callback(SchedulerTick{++sequence, next_ns, actual_ns, lateness_ns});
    } catch (...) {
      // callback 异常不能越过线程入口；记录失败并停止，交由监督层决定进程策略。
      worker_error_.store(ECANCELED, std::memory_order_relaxed);
      break;
    }

    auto finished_result = monotonic_now_ns();
    if (!finished_result) {
      worker_error_.store(EIO, std::memory_order_relaxed);
      break;
    }
    const std::int64_t finished_ns = finished_result.value();
    std::uint64_t missed = 0;
    if (finished_ns >= next_ns + period_ns) {
      // miss 按跨过的计划边界计数，而非简单的“这一轮是否迟到”布尔值。例如完成时
      // 已越过 3 个边界，就记录 3，并一次跳过这些过期回调。
      missed = static_cast<std::uint64_t>((finished_ns - next_ns) / period_ns);
      deadline_misses_.fetch_add(missed, std::memory_order_relaxed);
    }
    // 跨周期时直接跳到未来绝对边界，避免连续补跑旧周期形成“追赶风暴”。
    next_ns += static_cast<std::int64_t>(missed + 1) * period_ns;
  }

  // 无论正常 stop、时钟错误还是 callback 异常，都最后发布 running=false。
  // LinuxRuntime 的发布端和消费端据此 fail closed；daemon 后续再负责进程级升级。
  running_.store(false, std::memory_order_release);
}

}  // namespace rcr
