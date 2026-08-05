// RT3-L：优先级反转 vs PTHREAD_PRIO_INHERIT 对照。
// 低优先级持锁做短工作；高优先级抢同一把锁；中优先级忙等制造反转窗口。
// 需要 SCHED_FIFO；失败记 permission_denied / 退出 77，不假装测到了 PI。
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>
#include <thread>

#include <pthread.h>
#include <sched.h>

namespace {

struct Options {
  int work_ms{50};
  bool use_pi{true};
  bool self_check{false};
};

bool parse(int argc, char** argv, Options& opt) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view a(argv[i]);
    if (a == "--self-check") {
      opt.self_check = true;
    } else if (a == "--no-pi") {
      opt.use_pi = false;
    } else if (a == "--pi") {
      opt.use_pi = true;
    } else if (a == "--help" || a == "-h") {
      return false;
    } else if (a == "--work-ms") {
      if (i + 1 >= argc) {
        return false;
      }
      opt.work_ms = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    } else {
      return false;
    }
  }
  return opt.work_ms > 0 && opt.work_ms <= 5000;
}

bool set_fifo(int priority, int& err) {
  sched_param sp{};
  sp.sched_priority = priority;
  if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
    err = errno;
    return false;
  }
  // 必须同核，否则中优先级忙等跑在另一核上，看不到经典反转。
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(0, &set);
  if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0) {
    err = errno;
    return false;
  }
  err = 0;
  return true;
}

struct Shared {
  pthread_mutex_t mutex{};
  std::chrono::steady_clock::time_point high_wait_start{};
  std::chrono::steady_clock::time_point high_got_lock{};
  int low_errno{0};
  int mid_errno{0};
  int high_errno{0};
  bool low_started{false};
};

}  // namespace

int main(int argc, char** argv) {
  Options opt{};
  if (!parse(argc, argv, opt)) {
    std::cerr << "usage: " << argv[0] << " [--work-ms N] [--pi|--no-pi] [--self-check]\n";
    return EXIT_FAILURE;
  }

  Shared shared{};
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  // 默认协议 vs 优先级继承：只改这一处变量。
  if (opt.use_pi) {
    if (pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT) != 0) {
      std::cout << "result=unsupported\n"
                << "unsupported_reason=pthread_mutexattr_setprotocol\n";
      return opt.self_check ? 77 : EXIT_FAILURE;
    }
  }
  pthread_mutex_init(&shared.mutex, &attr);
  pthread_mutexattr_destroy(&attr);

  std::cout << "experiment=rt3_pi_mutex\n"
            << "use_pi=" << (opt.use_pi ? 1 : 0) << "\n"
            << "work_ms=" << opt.work_ms << "\n";

  std::thread low([&] {
    if (!set_fifo(10, shared.low_errno)) {
      return;
    }
    pthread_mutex_lock(&shared.mutex);
    shared.low_started = true;
    // 持锁期间做可见工作，拉长高优先级等待窗口。
    const auto until =
        std::chrono::steady_clock::now() + std::chrono::milliseconds{opt.work_ms};
    while (std::chrono::steady_clock::now() < until) {
    }
    pthread_mutex_unlock(&shared.mutex);
  });

  // 等到低优先级确实持锁后再拉起中/高优先级。
  for (int i = 0; i < 200 && !shared.low_started; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }

  std::thread mid([&] {
    if (!set_fifo(20, shared.mid_errno)) {
      return;
    }
    const auto until =
        std::chrono::steady_clock::now() + std::chrono::milliseconds{opt.work_ms * 2};
    while (std::chrono::steady_clock::now() < until) {
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds{2});

  std::thread high([&] {
    if (!set_fifo(30, shared.high_errno)) {
      return;
    }
    shared.high_wait_start = std::chrono::steady_clock::now();
    pthread_mutex_lock(&shared.mutex);
    shared.high_got_lock = std::chrono::steady_clock::now();
    pthread_mutex_unlock(&shared.mutex);
  });

  low.join();
  mid.join();
  high.join();
  pthread_mutex_destroy(&shared.mutex);

  if (shared.low_errno != 0 || shared.mid_errno != 0 || shared.high_errno != 0) {
    std::cout << "fifo_errno_low=" << shared.low_errno << "\n"
              << "fifo_errno_mid=" << shared.mid_errno << "\n"
              << "fifo_errno_high=" << shared.high_errno << "\n"
              << "result=permission_denied\n";
    return opt.self_check ? 77 : EXIT_FAILURE;
  }

  if (!shared.low_started ||
      shared.high_got_lock.time_since_epoch().count() == 0) {
    std::cout << "result=failed\n"
              << "detail=lock_handshake_incomplete\n";
    return EXIT_FAILURE;
  }

  const auto wait_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           shared.high_got_lock - shared.high_wait_start)
                           .count();
  std::cout << "high_wait_ns=" << wait_ns << "\n"
            << "result=pass\n";

  if (opt.self_check && wait_ns <= 0) {
    std::cerr << "self-check failed: expected positive wait\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
