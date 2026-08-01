// 该工具只测 Linux 周期线程，不代表硬实时保证或端到端设备性能。
#include "rcr/scheduler.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

struct Options {
  std::int64_t duration_ms{1000};
  std::int64_t period_us{1000};
  int fifo_priority{0};
  bool require_fifo{false};
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
    if (index + 1 >= argc) {
      return false;
    }
    std::int64_t value = 0;
    if (!parse_integer(argv[++index], value)) {
      return false;
    }
    if (arg == "--duration-ms") {
      options.duration_ms = value;
    } else if (arg == "--period-us") {
      options.period_us = value;
    } else if (arg == "--fifo-priority") {
      if (value < 0 || value > 99) {
        return false;
      }
      options.fifo_priority = static_cast<int>(value);
    } else {
      return false;
    }
  }
  // 限制单次工具运行范围，避免命令行整数在 chrono 转换时溢出或误跑数天。
  return options.duration_ms > 0 && options.duration_ms <= 86'400'000 &&
         options.period_us > 0 && options.period_us <= 60'000'000;
}

void usage(const char* program) {
  std::cerr << "usage: " << program
            << " [--duration-ms N] [--period-us N] [--fifo-priority 0..99]"
               " [--require-fifo]\n";
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
  rcr::PeriodicScheduler scheduler(config);
  const auto start = scheduler.start([](const rcr::SchedulerTick&) {
    // 基线 callback 刻意为空，只测线程唤醒；业务负载应作为单独对照组记录。
  });
  if (!start) {
    std::cerr << "benchmark start failed: " << rcr::to_string(start.error().code()) << ": "
              << start.error().message() << "\n";
    return EXIT_FAILURE;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds{options.duration_ms});
  scheduler.request_stop();
  scheduler.join();
  const rcr::SchedulerStats stats = scheduler.stats();

  // 行式 key=value 便于 shell 收集，也避免为基准工具引入 JSON/YAML 依赖。
  std::cout << "duration_ms=" << options.duration_ms << "\n"
            << "period_us=" << options.period_us << "\n"
            << "fifo_priority_requested=" << options.fifo_priority << "\n"
            << "fifo_enabled=" << (stats.fifo_enabled ? 1 : 0) << "\n"
            << "fifo_error=" << stats.fifo_error << "\n"
            << "worker_error=" << stats.worker_error << "\n"
            << "cycles=" << stats.cycles << "\n"
            << "deadline_misses=" << stats.deadline_misses << "\n"
            << "lateness_min_ns=" << stats.min_lateness_ns << "\n"
            << "lateness_mean_ns=" << stats.mean_lateness_ns << "\n"
            << "lateness_max_ns=" << stats.max_lateness_ns << "\n";
  return EXIT_SUCCESS;
}
