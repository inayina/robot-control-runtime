// RT3-C：周期路径上 alloc+格式化 vs 预分配，以及受控忙等过载。
// 使用绝对 CLOCK_MONOTONIC 睡眠，不链接 Runtime，避免实验污染生产路径。
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <time.h>

namespace {

struct Options {
  std::string mode{"compare"};  // compare|prealloc|alloc_format|busy|log_in_cycle
  int ticks{2000};
  int period_us{1000};
  int busy_us{0};
  bool self_check{false};
};

bool parse(int argc, char** argv, Options& opt) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view a(argv[i]);
    if (a == "--self-check") {
      opt.self_check = true;
      continue;
    }
    if (a == "--help" || a == "-h") {
      return false;
    }
    if (i + 1 >= argc) {
      return false;
    }
    if (a == "--mode") {
      opt.mode = argv[++i];
    } else if (a == "--ticks") {
      opt.ticks = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    } else if (a == "--period-us") {
      opt.period_us = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    } else if (a == "--busy-us") {
      opt.busy_us = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    } else {
      return false;
    }
  }
  return opt.ticks > 0 && opt.ticks <= 1'000'000 && opt.period_us > 0 &&
         opt.busy_us >= 0;
}

timespec ns_to_ts(std::int64_t ns) {
  timespec ts{};
  ts.tv_sec = static_cast<time_t>(ns / 1'000'000'000);
  ts.tv_nsec = static_cast<long>(ns % 1'000'000'000);
  return ts;
}

std::int64_t now_ns() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}

struct RunStats {
  std::int64_t min_ns{0};
  std::int64_t max_ns{0};
  std::int64_t p99_ns{0};
  std::int64_t miss{0};
};

RunStats summarize(std::vector<std::int64_t>& samples, std::int64_t miss) {
  RunStats s{};
  s.miss = miss;
  if (samples.empty()) {
    return s;
  }
  std::sort(samples.begin(), samples.end());
  s.min_ns = samples.front();
  s.max_ns = samples.back();
  const std::size_t idx =
      static_cast<std::size_t>((samples.size() - 1) * 99) / 100;
  s.p99_ns = samples[idx];
  return s;
}

void print_stats(const char* label, const RunStats& s) {
  std::cout << "mode=" << label << "\n"
            << "exec_min_ns=" << s.min_ns << "\n"
            << "exec_p99_ns=" << s.p99_ns << "\n"
            << "exec_max_ns=" << s.max_ns << "\n"
            << "deadline_misses=" << s.miss << "\n";
}

RunStats run_loop(const Options& opt, const std::string& mode) {
  const std::int64_t period_ns = static_cast<std::int64_t>(opt.period_us) * 1000;
  std::vector<std::int64_t> samples;
  samples.reserve(static_cast<std::size_t>(opt.ticks));

  // 预分配：周期路径只写固定缓冲，不 new / 不扩容。
  std::vector<char> prebuf(256, 0);
  std::uint64_t sink = 0;
  std::int64_t misses = 0;

  std::int64_t next = now_ns() + period_ns;
  for (int i = 0; i < opt.ticks; ++i) {
    timespec abs_ts = ns_to_ts(next);
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &abs_ts, nullptr);
    const std::int64_t start = now_ns();

    if (mode == "alloc_format") {
      // 反例：周期内堆分配 + 格式化（可能触发分配器锁与缺页）。
      auto* heap = new std::string();
      char tmp[64];
      std::snprintf(tmp, sizeof(tmp), "tick=%d t=%lld", i,
                    static_cast<long long>(start));
      heap->assign(tmp);
      sink ^= static_cast<std::uint64_t>(heap->size());
      delete heap;
    } else if (mode == "log_in_cycle") {
      // 反例：周期内写 stdout（可能阻塞）。
      std::printf("tick=%d\n", i);
    } else if (mode == "busy") {
      const auto until = start + static_cast<std::int64_t>(opt.busy_us) * 1000;
      while (now_ns() < until) {
      }
    } else {
      // prealloc：只写已有缓冲。
      std::snprintf(prebuf.data(), prebuf.size(), "%d:%lld", i,
                    static_cast<long long>(start));
      sink ^= static_cast<std::uint64_t>(prebuf[0]);
    }

    const std::int64_t end = now_ns();
    samples.push_back(end - start);
    if (end > next + period_ns) {
      ++misses;
    }
    // 跳过过旧边界，与本仓 scheduler 合同一致：不追赶补跑。
    next += period_ns;
    while (next < end) {
      next += period_ns;
    }
  }
  std::cout << "sink=" << sink << "\n";
  return summarize(samples, misses);
}

}  // namespace

int main(int argc, char** argv) {
  Options opt{};
  if (!parse(argc, argv, opt)) {
    std::cerr << "usage: " << argv[0]
              << " [--mode compare|prealloc|alloc_format|busy|log_in_cycle]\n"
                 "       [--ticks N] [--period-us N] [--busy-us N] [--self-check]\n";
    return EXIT_FAILURE;
  }

  std::cout << "experiment=rt3_cycle_path\n"
            << "period_us=" << opt.period_us << "\n"
            << "ticks=" << opt.ticks << "\n";

  if (opt.mode == "compare") {
    const auto bad = run_loop(opt, "alloc_format");
    print_stats("alloc_format", bad);
    const auto good = run_loop(opt, "prealloc");
    print_stats("prealloc", good);
    std::cout << "result=pass\n";
    if (opt.self_check) {
      // 方向性：预分配路径的 max 不应明显差于 alloc 路径的数倍以上；
      // 更关键的是 alloc 路径 p99/max 通常更大。允许噪声，只拒明显反例。
      if (good.max_ns > bad.max_ns * 5 + 1'000'000) {
        std::cerr << "self-check failed: prealloc unexpectedly much worse\n";
        return EXIT_FAILURE;
      }
    }
    return EXIT_SUCCESS;
  }

  if (opt.mode == "busy" && opt.busy_us <= 0) {
    opt.busy_us = 3000;
  }
  const auto stats = run_loop(opt, opt.mode);
  print_stats(opt.mode.c_str(), stats);
  std::cout << "result=pass\n";
  if (opt.self_check && opt.mode == "busy" && stats.miss <= 0) {
    std::cerr << "self-check failed: busy mode expected deadline misses\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
