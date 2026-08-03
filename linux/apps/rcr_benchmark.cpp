// 默认空 callback 只测 Linux 周期线程唤醒 lateness；可选 --callback-delay-us 做受控过载，
// 验证 miss 与跳过旧边界。不含 CAN/状态机，也不代表硬实时或端到端设备性能。
#include "rcr/scheduler.hpp"
#include "rcr/stats.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sched.h>

namespace {

struct Options {
  std::int64_t duration_ms{1000};
  std::int64_t period_us{1000};
  /// 0=关闭。大于 0 时在采样后 sleep，故意让 callback 跨过多个 period 边界。
  /// 这是 A-T 过载实验开关，不是生产 Runtime 行为；默认关闭避免污染空载基线。
  std::int64_t callback_delay_us{0};
  int fifo_priority{0};
  bool require_fifo{false};
  int cpu_affinity{-1};
  std::string samples_path{};
  bool enable_samples{true};
};

bool parse_integer(std::string_view text, std::int64_t& value) {
  try {
    std::size_t used = 0;
    const std::string input(text);
    value = std::stoll(input, &used, 10);
    return used == input.size();
  } catch (...) {
    return false;
  }
}

bool parse_options(int argc, char** argv, Options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg(argv[index]);
    if (arg == "--require-fifo") {
      options.require_fifo = true;
      continue;
    }
    if (arg == "--no-samples") {
      // 对照实验：关闭采样，观察工具本身对调度的扰动上界。
      options.enable_samples = false;
      continue;
    }
    if (arg == "--help" || arg == "-h") {
      return false;
    }
    if (index + 1 >= argc) {
      return false;
    }
    if (arg == "--samples-out") {
      options.samples_path = argv[++index];
      continue;
    }
    std::int64_t value = 0;
    if (!parse_integer(argv[++index], value)) {
      return false;
    }
    if (arg == "--duration-ms") {
      options.duration_ms = value;
    } else if (arg == "--period-us") {
      options.period_us = value;
    } else if (arg == "--callback-delay-us") {
      // 允许 0（关闭）。正值表示人工占用 worker 的时长，不是 lateness 本身。
      if (value < 0) {
        return false;
      }
      options.callback_delay_us = value;
    } else if (arg == "--fifo-priority") {
      if (value < 0 || value > 99) {
        return false;
      }
      options.fifo_priority = static_cast<int>(value);
    } else if (arg == "--cpu-affinity") {
      if (value < 0 || value >= CPU_SETSIZE) {
        return false;
      }
      options.cpu_affinity = static_cast<int>(value);
    } else {
      return false;
    }
  }
  return options.duration_ms > 0 && options.duration_ms <= 86'400'000 &&
         options.period_us > 0 && options.period_us <= 60'000'000 &&
         options.callback_delay_us >= 0 &&
         options.callback_delay_us <= 60'000'000;
}

void usage(const char* program) {
  std::cerr << "usage: " << program
            << " [--duration-ms N] [--period-us N] [--callback-delay-us N]\n"
               "       [--fifo-priority 0..99] [--require-fifo] [--cpu-affinity N]\n"
               "       [--samples-out PATH] [--no-samples]\n"
               "Default empty callback measures wakeup lateness only; not CAN latency.\n"
               "--callback-delay-us injects sleep after sampling (default 0=off).\n";
}

}  // namespace

int main(int argc, char** argv) {
  Options options{};
  if (!parse_options(argc, argv, options)) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  rcr::SchedulerConfig config{};
  config.period = std::chrono::microseconds{options.period_us};
  config.fifo_priority = options.fifo_priority;
  config.require_fifo = options.require_fifo;
  config.cpu_affinity = options.cpu_affinity;

  // 预分配：按名义周期估算容量并留 25% 裕量，避免周期路径 vector 扩容。
  const std::size_t expected =
      static_cast<std::size_t>(options.duration_ms * 1000 / options.period_us) + 16;
  const std::size_t capacity = expected + expected / 4 + 64;
  std::vector<std::int64_t> samples;
  if (options.enable_samples) {
    samples.resize(capacity);
  }
  std::atomic<std::size_t> sample_count{0};
  std::atomic<bool> sample_overflow{false};

  rcr::PeriodicScheduler scheduler(config);
  const auto start = scheduler.start([&](const rcr::SchedulerTick& tick) {
    if (options.enable_samples) {
      // 先记录 wakeup lateness：该值在 scheduler 进入 callback 前已算好，不含本次 delay。
      // 周期路径只写固定槽位整数，不排序、不写磁盘、不分配。
      const std::size_t index = sample_count.fetch_add(1, std::memory_order_relaxed);
      if (index < samples.size()) {
        samples[index] = tick.wakeup_lateness_ns;
      } else {
        sample_overflow.store(true, std::memory_order_relaxed);
      }
    }
    if (options.callback_delay_us > 0) {
      // sleep 占用 worker，迫使 finished_ns 越过多个绝对边界；scheduler 应记 miss 并跳过
      // 旧周期。这不是测量 lateness 的手段，而是验证过载合同的故障注入。
      std::this_thread::sleep_for(std::chrono::microseconds{options.callback_delay_us});
    }
  });
  if (!start) {
    // 启动失败也输出机器可读的实际 syscall 结果；矩阵据此区分权限不足与配置/代码失败，
    // 不能通过 stderr 文本猜测错误类型。
    const rcr::SchedulerStats failed_stats = scheduler.stats();
    std::cout << "fifo_priority_requested=" << options.fifo_priority << "\n"
              << "fifo_enabled=" << (failed_stats.fifo_enabled ? 1 : 0) << "\n"
              << "fifo_error=" << failed_stats.fifo_error << "\n"
              << "cpu_affinity_requested=" << options.cpu_affinity << "\n"
              << "affinity_enabled=" << (failed_stats.affinity_enabled ? 1 : 0) << "\n"
              << "affinity_error=" << failed_stats.affinity_error << "\n"
              << "worker_error=" << failed_stats.worker_error << "\n";
    std::cerr << "benchmark start failed: " << rcr::to_string(start.error().code()) << ": "
              << start.error().message() << "\n";
    return EXIT_FAILURE;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds{options.duration_ms});
  scheduler.request_stop();
  scheduler.join();
  const rcr::SchedulerStats stats = scheduler.stats();
  const std::size_t count = std::min(sample_count.load(std::memory_order_relaxed), samples.size());

  // callback_delay_us 与 lateness_* 语义不同：前者是人工执行时间，后者是唤醒相对计划边界。
  std::cout << "duration_ms=" << options.duration_ms << "\n"
            << "period_us=" << options.period_us << "\n"
            << "callback_delay_us=" << options.callback_delay_us << "\n"
            << "fifo_priority_requested=" << options.fifo_priority << "\n"
            << "fifo_enabled=" << (stats.fifo_enabled ? 1 : 0) << "\n"
            << "fifo_error=" << stats.fifo_error << "\n"
            << "worker_error=" << stats.worker_error << "\n"
            << "cpu_affinity_requested=" << options.cpu_affinity << "\n"
            << "affinity_enabled=" << (stats.affinity_enabled ? 1 : 0) << "\n"
            << "affinity_error=" << stats.affinity_error << "\n"
            << "samples_enabled=" << (options.enable_samples ? 1 : 0) << "\n"
            << "sample_count=" << count << "\n"
            << "sample_overflow=" << (sample_overflow.load() ? 1 : 0) << "\n"
            << "cycles=" << stats.cycles << "\n"
            << "deadline_misses=" << stats.deadline_misses << "\n"
            << "lateness_min_ns=" << stats.min_lateness_ns << "\n"
            << "lateness_mean_ns=" << stats.mean_lateness_ns << "\n"
            << "lateness_max_ns=" << stats.max_lateness_ns << "\n"
            << "percentile_algorithm=" << rcr::percentile_algorithm_id() << "\n";

  if (options.enable_samples && count > 0) {
    auto summary = rcr::summarize_lateness_ns(std::span<const std::int64_t>(samples.data(), count));
    if (!summary) {
      std::cerr << "percentile failed: " << summary.error().message() << "\n";
      return EXIT_FAILURE;
    }
    std::cout << "lateness_p50_ns=" << summary.value().p50_ns << "\n"
              << "lateness_p95_ns=" << summary.value().p95_ns << "\n"
              << "lateness_p99_ns=" << summary.value().p99_ns << "\n"
              << "lateness_p99_9_ns=" << summary.value().p99_9_ns << "\n";

    if (!options.samples_path.empty()) {
      std::ofstream out(options.samples_path, std::ios::out | std::ios::trunc);
      if (!out) {
        std::cerr << "failed to open samples-out path\n";
        return EXIT_FAILURE;
      }
      for (std::size_t i = 0; i < count; ++i) {
        out << samples[i] << '\n';
      }
    }
  }

  // require-fifo 已在 start 失败；非强制但请求 FIFO 却未生效时，由调用方脚本解释为
  // permission_denied，本工具仍返回 0 并输出 fifo_enabled=0。
  if (sample_overflow.load()) {
    std::cerr << "warning: sample buffer overflow; increase duration/period margin\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
