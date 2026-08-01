// 该工具只测 Linux 周期线程的唤醒基线，不包含 CAN、状态机或业务 callback 负载。
// 输出是某台机器、某次内核/权限/负载条件下的样本，不代表硬实时保证或端到端设备性能。
#include "rcr/scheduler.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

struct Options {
  // CLI 使用人类易读的 ms/us；进入 SchedulerConfig 时统一转换为 chrono，避免业务代码
  // 继续携带“裸整数到底是什么单位”的歧义。
  std::int64_t duration_ms{1000};
  std::int64_t period_us{1000};
  int fifo_priority{0};
  bool require_fifo{false};
};

bool parse_integer(std::string_view text, std::int64_t& value) {
  // stoll 允许前缀数字，因此必须检查 used==size，拒绝 "100ms" 这类部分解析成功输入。
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
  // start 会等待 worker 完成 FIFO/时钟启动握手；返回成功后再开始统计运行时长，避免把
  // 线程创建和权限申请误算进周期样本。
  const auto start = scheduler.start([](const rcr::SchedulerTick&) {
    // 基线 callback 刻意为空，只测线程唤醒；业务负载应作为单独对照组记录。
  });
  if (!start) {
    std::cerr << "benchmark start failed: " << rcr::to_string(start.error().code()) << ": "
              << start.error().message() << "\n";
    return EXIT_FAILURE;
  }

  // main 线程只负责控制 benchmark 持续时间；真正周期由独立 scheduler worker 驱动。
  // sleep_for 的精度不会决定 worker period，只可能让总采样时长略长。
  std::this_thread::sleep_for(std::chrono::milliseconds{options.duration_ms});
  scheduler.request_stop();
  scheduler.join();
  const rcr::SchedulerStats stats = scheduler.stats();

  // join 后统计不再变化，此时读取可得到稳定最终值。行式 key=value 便于 shell 收集，
  // 也避免为一个基准工具引入 JSON/YAML 依赖；环境元数据由阶段 3 证据脚本补齐。
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
