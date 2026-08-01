// Orange Pi/Linux Runtime 实现；不得加入 MCU HAL、FreeRTOS 或 ESP-IDF 依赖。
#include "rcr/scheduler.hpp"

#include "rcr/time.hpp"

#include <cerrno>
#include <cstring>
#include <limits>
#include <pthread.h>
#include <string>
#include <utility>

namespace rcr {

PeriodicScheduler::PeriodicScheduler(SchedulerConfig config) : config_(config) {}

PeriodicScheduler::~PeriodicScheduler() {
  request_stop();
  join();
}

Result<void> PeriodicScheduler::start(TickCallback callback) {
  if (config_.period.count() <= 0) {
    return Error{Errc::InvalidArgument, "scheduler period must be positive"};
  }
  if (config_.fifo_priority < 0 || config_.fifo_priority > 99) {
    return Error{Errc::InvalidArgument, "SCHED_FIFO priority must be 0..99"};
  }
  if (config_.require_fifo && config_.fifo_priority == 0) {
    return Error{Errc::InvalidArgument, "require_fifo needs a non-zero priority"};
  }
  if (!callback) {
    return Error{Errc::InvalidArgument, "scheduler callback is empty"};
  }
  if (thread_.joinable()) {
    return Error{Errc::Busy, "scheduler already started"};
  }

  stop_requested_.store(false, std::memory_order_release);
  running_.store(false, std::memory_order_release);
  cycles_.store(0, std::memory_order_relaxed);
  deadline_misses_.store(0, std::memory_order_relaxed);
  min_lateness_ns_.store(0, std::memory_order_relaxed);
  max_lateness_ns_.store(0, std::memory_order_relaxed);
  total_lateness_ns_.store(0, std::memory_order_relaxed);
  fifo_enabled_.store(false, std::memory_order_relaxed);
  fifo_error_.store(0, std::memory_order_relaxed);
  worker_error_.store(0, std::memory_order_relaxed);
  {
    std::lock_guard lock(startup_mutex_);
    startup_done_ = false;
    startup_error_ = Error{};
  }

  try {
    thread_ = std::thread(&PeriodicScheduler::run, this, std::move(callback));
  } catch (const std::system_error& error) {
    return Error{Errc::IoError, std::string("create scheduler thread: ") + error.what()};
  }

  std::unique_lock lock(startup_mutex_);
  startup_cv_.wait(lock, [this] { return startup_done_; });
  const Error startup_error = startup_error_;
  lock.unlock();
  if (startup_error) {
    join();
    return startup_error;
  }
  return Result<void>::success();
}

void PeriodicScheduler::request_stop() noexcept {
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
  value.worker_error = worker_error_.load(std::memory_order_relaxed);
  return value;
}

const SchedulerConfig& PeriodicScheduler::config() const noexcept { return config_; }

void PeriodicScheduler::run(TickCallback callback) {
  Error startup_error{};
  if (config_.fifo_priority > 0) {
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

  auto now_result = monotonic_now_ns();
  if (!now_result && !startup_error) {
    startup_error = now_result.error();
  }

  {
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
  std::int64_t next_ns = now_result.value() + period_ns;
  std::uint64_t sequence = 0;

  while (!stop_requested_.load(std::memory_order_acquire)) {
    const timespec deadline = ns_to_timespec(next_ns);
    int sleep_rc = 0;
    do {
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
      missed = static_cast<std::uint64_t>((finished_ns - next_ns) / period_ns);
      deadline_misses_.fetch_add(missed, std::memory_order_relaxed);
    }
    // 跨周期时直接跳到未来绝对边界，避免连续补跑旧周期形成“追赶风暴”。
    next_ns += static_cast<std::int64_t>(missed + 1) * period_ns;
  }

  running_.store(false, std::memory_order_release);
}

}  // namespace rcr
