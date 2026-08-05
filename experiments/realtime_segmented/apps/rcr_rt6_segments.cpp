// RT6：周期唤醒 → callback → 入队/eventfd → I/O 线程 → 软件 peer ACK 的分段时延。
// 不修改 Runtime Core / CanIoLoop；Orange Pi 无 CAN 时仍可测软件路径。
// 队列满时丢弃并可见计数，不静默覆盖未处理样本。

#include "rcr/owned_fd.hpp"
#include "rcr/scheduler.hpp"
#include "rcr/time.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <errno.h>
#include <sys/epoll.h>
#include <unistd.h>

namespace {

struct Options {
  std::string mode{"baseline"};  // baseline|cb_busy|io_busy|compare
  int ticks{2000};
  int period_us{1000};
  int busy_us{500};
  int fifo{0};
  int cpu{-1};
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
    } else if (a == "--fifo") {
      opt.fifo = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    } else if (a == "--cpu") {
      opt.cpu = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    } else {
      return false;
    }
  }
  return opt.ticks > 0 && opt.ticks <= 1'000'000 && opt.period_us > 0 && opt.busy_us >= 0 &&
         opt.fifo >= 0 && opt.fifo <= 99;
}

void busy_spin_us(int us) {
  if (us <= 0) {
    return;
  }
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::microseconds(us);
  while (std::chrono::steady_clock::now() < deadline) {
  }
}

std::int64_t now_ns() {
  auto r = rcr::monotonic_now_ns();
  return r.ok() ? r.value() : 0;
}

std::int64_t percentile_sorted(std::vector<std::int64_t>& v, double p) {
  if (v.empty()) {
    return 0;
  }
  std::sort(v.begin(), v.end());
  const auto idx = static_cast<std::size_t>(p * static_cast<double>(v.size() - 1));
  return v[idx];
}

struct SegStats {
  std::int64_t min_ns{0};
  std::int64_t p50_ns{0};
  std::int64_t p99_ns{0};
  std::int64_t max_ns{0};
  std::uint64_t n{0};
};

SegStats summarize(std::vector<std::int64_t> samples) {
  SegStats s{};
  s.n = samples.size();
  if (samples.empty()) {
    return s;
  }
  auto tmp = samples;
  std::sort(tmp.begin(), tmp.end());
  s.min_ns = tmp.front();
  s.max_ns = tmp.back();
  s.p50_ns = percentile_sorted(samples, 0.50);
  s.p99_ns = percentile_sorted(samples, 0.99);
  return s;
}

void print_seg(const char* name, const SegStats& s) {
  std::cout << name << "_n=" << s.n << '\n'
            << name << "_min_ns=" << s.min_ns << '\n'
            << name << "_p50_ns=" << s.p50_ns << '\n'
            << name << "_p99_ns=" << s.p99_ns << '\n'
            << name << "_max_ns=" << s.max_ns << '\n';
}

// 单生产者单消费者环形队列：满则拒绝推入（drop），不覆盖未消费项。
struct PublishSlot {
  std::uint64_t seq{0};
  std::int64_t t_sched{0};
  std::int64_t t_wake{0};
  std::int64_t t_cb_begin{0};
  std::int64_t t_publish{0};
  std::int64_t t_cb_end{0};
};

class SpscQueue {
 public:
  explicit SpscQueue(std::size_t capacity)
      : capacity_(capacity), storage_(capacity) {}

  bool try_push(const PublishSlot& slot) {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto next = (head + 1) % capacity_;
    if (next == tail_.load(std::memory_order_acquire)) {
      return false;
    }
    storage_[head] = slot;
    head_.store(next, std::memory_order_release);
    return true;
  }

  bool try_pop(PublishSlot& out) {
    const auto tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      return false;
    }
    out = storage_[tail];
    tail_.store((tail + 1) % capacity_, std::memory_order_release);
    return true;
  }

 private:
  std::size_t capacity_;
  std::vector<PublishSlot> storage_;
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

struct Completed {
  std::int64_t wakeup_ns{0};
  std::int64_t callback_ns{0};
  std::int64_t queue_ns{0};
  std::int64_t io_ack_ns{0};
  std::int64_t e2e_ns{0};
};

struct RunResult {
  std::string mode;
  std::string result{"pass"};
  int fifo_error{0};
  bool fifo_enabled{false};
  std::uint64_t cycles{0};
  std::uint64_t deadline_misses{0};
  std::uint64_t drops{0};
  std::uint64_t completed{0};
  SegStats wakeup{};
  SegStats callback{};
  SegStats queue{};
  SegStats io_ack{};
  SegStats e2e{};
};

RunResult run_once(const Options& opt, const std::string& mode) {
  RunResult out;
  out.mode = mode;

  const bool cb_busy = (mode == "cb_busy");
  const bool io_busy = (mode == "io_busy");
  const int busy_us = (cb_busy || io_busy) ? opt.busy_us : 0;

  auto notify = rcr::EventFd::create();
  auto stop_fd = rcr::EventFd::create();
  if (!notify.ok() || !stop_fd.ok()) {
    out.result = "failed";
    return out;
  }
  rcr::EventFd wake = std::move(notify.value());
  rcr::EventFd stop = std::move(stop_fd.value());

  // capacity 取 ticks+8，保留一格空位；过小会人为制造 drop。
  const std::size_t qcap =
      static_cast<std::size_t>(std::max(opt.ticks + 8, 64));
  SpscQueue queue(qcap);
  std::vector<Completed> done;
  done.reserve(static_cast<std::size_t>(opt.ticks));
  std::mutex done_mu;
  std::atomic<std::uint64_t> drops{0};
  std::atomic<bool> producer_done{false};

  const int epfd = ::epoll_create1(EPOLL_CLOEXEC);
  if (epfd < 0) {
    out.result = "failed";
    return out;
  }
  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = wake.native_handle();
  if (::epoll_ctl(epfd, EPOLL_CTL_ADD, wake.native_handle(), &ev) != 0) {
    ::close(epfd);
    out.result = "failed";
    return out;
  }
  ev.data.fd = stop.native_handle();
  if (::epoll_ctl(epfd, EPOLL_CTL_ADD, stop.native_handle(), &ev) != 0) {
    ::close(epfd);
    out.result = "failed";
    return out;
  }

  std::thread io([&] {
    epoll_event events[4];
    for (;;) {
      const int n = ::epoll_wait(epfd, events, 4, 200);
      if (n < 0 && errno == EINTR) {
        continue;
      }
      if (n < 0) {
        break;
      }
      bool saw_stop = false;
      bool saw_wake = false;
      for (int i = 0; i < n; ++i) {
        if (events[i].data.fd == stop.native_handle()) {
          saw_stop = true;
        }
        if (events[i].data.fd == wake.native_handle()) {
          saw_wake = true;
        }
      }
      if (saw_wake) {
        (void)wake.drain();
      }
      PublishSlot slot{};
      while (queue.try_pop(slot)) {
        const std::int64_t t_io = now_ns();
        if (io_busy) {
          busy_spin_us(busy_us);
        }
        const std::int64_t t_ack = now_ns();
        Completed c{};
        c.wakeup_ns = std::max<std::int64_t>(0, slot.t_wake - slot.t_sched);
        c.callback_ns = std::max<std::int64_t>(0, slot.t_cb_end - slot.t_cb_begin);
        c.queue_ns = std::max<std::int64_t>(0, t_io - slot.t_publish);
        c.io_ack_ns = std::max<std::int64_t>(0, t_ack - t_io);
        c.e2e_ns = std::max<std::int64_t>(0, t_ack - slot.t_sched);
        std::lock_guard<std::mutex> lock(done_mu);
        done.push_back(c);
      }
      if (saw_stop) {
        (void)stop.drain();
        // 排空残余后再退出，避免尾部样本丢失。
        while (queue.try_pop(slot)) {
          const std::int64_t t_io = now_ns();
          const std::int64_t t_ack = now_ns();
          Completed c{};
          c.wakeup_ns = std::max<std::int64_t>(0, slot.t_wake - slot.t_sched);
          c.callback_ns = std::max<std::int64_t>(0, slot.t_cb_end - slot.t_cb_begin);
          c.queue_ns = std::max<std::int64_t>(0, t_io - slot.t_publish);
          c.io_ack_ns = std::max<std::int64_t>(0, t_ack - t_io);
          c.e2e_ns = std::max<std::int64_t>(0, t_ack - slot.t_sched);
          std::lock_guard<std::mutex> lock(done_mu);
          done.push_back(c);
        }
        if (producer_done.load(std::memory_order_acquire)) {
          break;
        }
      }
      if (producer_done.load(std::memory_order_acquire) && n == 0) {
        // 超时且生产者结束：再尝试排空一次后退出。
        while (queue.try_pop(slot)) {
          const std::int64_t t_io = now_ns();
          const std::int64_t t_ack = now_ns();
          Completed c{};
          c.wakeup_ns = std::max<std::int64_t>(0, slot.t_wake - slot.t_sched);
          c.callback_ns = std::max<std::int64_t>(0, slot.t_cb_end - slot.t_cb_begin);
          c.queue_ns = std::max<std::int64_t>(0, t_io - slot.t_publish);
          c.io_ack_ns = std::max<std::int64_t>(0, t_ack - t_io);
          c.e2e_ns = std::max<std::int64_t>(0, t_ack - slot.t_sched);
          std::lock_guard<std::mutex> lock(done_mu);
          done.push_back(c);
        }
        break;
      }
    }
  });

  rcr::SchedulerConfig cfg{};
  cfg.period = std::chrono::microseconds(opt.period_us);
  cfg.fifo_priority = opt.fifo;
  cfg.require_fifo = opt.fifo > 0;
  cfg.cpu_affinity = opt.cpu;

  rcr::PeriodicScheduler scheduler(cfg);
  std::atomic<int> ticks_left{opt.ticks};

  auto started = scheduler.start([&](const rcr::SchedulerTick& tick) {
    // 先占一个名额；占到 0 说明已完成目标 ticks，停止且不再投递。
    const int left = ticks_left.fetch_sub(1, std::memory_order_relaxed);
    if (left <= 0) {
      scheduler.request_stop();
      return;
    }
    const std::int64_t t_cb_begin = now_ns();
    if (cb_busy) {
      busy_spin_us(busy_us);
    }
    PublishSlot slot{};
    slot.seq = tick.sequence;
    slot.t_sched = tick.scheduled_ns;
    slot.t_wake = tick.actual_ns;
    slot.t_cb_begin = t_cb_begin;
    // publish 时刻：入队前的单调时间；callback 段止于 t_cb_end（含 busy + 填槽）。
    slot.t_publish = now_ns();
    slot.t_cb_end = slot.t_publish;
    if (!queue.try_push(slot)) {
      drops.fetch_add(1, std::memory_order_relaxed);
    } else {
      // EventFd::signal_stop 即 write(1)；此处作跨线程 notify，不是进程退出。
      (void)wake.signal_stop();
    }
    if (left == 1) {
      scheduler.request_stop();
    }
  });

  if (!started.ok()) {
    producer_done.store(true, std::memory_order_release);
    (void)stop.signal_stop();
    io.join();
    ::close(epfd);
    if (opt.fifo > 0 && started.error().code() == rcr::Errc::Rejected) {
      out.result = "permission_denied";
      out.fifo_error = scheduler.stats().fifo_error;
    } else {
      out.result = "failed";
    }
    return out;
  }

  // 等周期线程跑完 ticks。
  while (scheduler.running()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  scheduler.join();
  const auto st = scheduler.stats();
  out.cycles = st.cycles;
  out.deadline_misses = st.deadline_misses;
  out.fifo_enabled = st.fifo_enabled;
  out.fifo_error = st.fifo_error;
  if (opt.fifo > 0 && !st.fifo_enabled) {
    producer_done.store(true, std::memory_order_release);
    (void)stop.signal_stop();
    io.join();
    ::close(epfd);
    out.result = "permission_denied";
    return out;
  }

  producer_done.store(true, std::memory_order_release);
  (void)stop.signal_stop();
  io.join();
  ::close(epfd);

  out.drops = drops.load(std::memory_order_relaxed);

  std::vector<std::int64_t> wakeup;
  std::vector<std::int64_t> callback;
  std::vector<std::int64_t> queue_v;
  std::vector<std::int64_t> io_ack;
  std::vector<std::int64_t> e2e;
  {
    std::lock_guard<std::mutex> lock(done_mu);
    out.completed = done.size();
    for (const auto& c : done) {
      wakeup.push_back(c.wakeup_ns);
      callback.push_back(c.callback_ns);
      queue_v.push_back(c.queue_ns);
      io_ack.push_back(c.io_ack_ns);
      e2e.push_back(c.e2e_ns);
    }
  }

  out.wakeup = summarize(std::move(wakeup));
  out.callback = summarize(std::move(callback));
  out.queue = summarize(std::move(queue_v));
  out.io_ack = summarize(std::move(io_ack));
  out.e2e = summarize(std::move(e2e));
  return out;
}

void print_run(const RunResult& r) {
  std::cout << "mode=" << r.mode << '\n'
            << "result=" << r.result << '\n'
            << "fifo_enabled=" << (r.fifo_enabled ? 1 : 0) << '\n'
            << "fifo_error=" << r.fifo_error << '\n'
            << "cycles=" << r.cycles << '\n'
            << "deadline_misses=" << r.deadline_misses << '\n'
            << "drops=" << r.drops << '\n'
            << "completed=" << r.completed << '\n';
  print_seg("wakeup", r.wakeup);
  print_seg("callback", r.callback);
  print_seg("queue", r.queue);
  print_seg("io_ack", r.io_ack);
  print_seg("e2e", r.e2e);
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  if (!parse(argc, argv, opt)) {
    std::cerr
        << "usage: rcr_rt6_segments --mode baseline|cb_busy|io_busy|compare "
           "[--ticks N] [--period-us N] [--busy-us N] [--fifo N] [--cpu N] "
           "[--self-check]\n";
    return 2;
  }

  if (opt.mode == "compare" || opt.self_check) {
    Options base = opt;
    auto r0 = run_once(base, "baseline");
    std::cout << "=== baseline ===\n";
    print_run(r0);
    if (r0.result == "permission_denied") {
      return 77;
    }
    if (r0.result != "pass" || r0.completed == 0) {
      return 1;
    }

    auto r1 = run_once(base, "cb_busy");
    std::cout << "=== cb_busy ===\n";
    print_run(r1);
    if (r1.result != "pass" || r1.completed == 0) {
      return 1;
    }

    auto r2 = run_once(base, "io_busy");
    std::cout << "=== io_busy ===\n";
    print_run(r2);
    if (r2.result != "pass" || r2.completed == 0) {
      return 1;
    }

    // self-check：忙等必须抬高对应段的 p50（留 2x 或至少 busy/2 余量）。
    if (opt.self_check) {
      const auto floor =
          static_cast<std::int64_t>(opt.busy_us) * 1000 / 2;  // ns
      if (r1.callback.p50_ns < floor) {
        std::cerr << "self-check: cb_busy callback_p50 too low: "
                  << r1.callback.p50_ns << " < " << floor << '\n';
        return 1;
      }
      if (r2.io_ack.p50_ns < floor) {
        std::cerr << "self-check: io_busy io_ack_p50 too low: "
                  << r2.io_ack.p50_ns << " < " << floor << '\n';
        return 1;
      }
      if (!(r1.callback.p50_ns > r0.callback.p50_ns)) {
        std::cerr << "self-check: cb_busy did not raise callback vs baseline\n";
        return 1;
      }
      if (!(r2.io_ack.p50_ns > r0.io_ack.p50_ns)) {
        std::cerr << "self-check: io_busy did not raise io_ack vs baseline\n";
        return 1;
      }
      std::cout << "result=pass\nself_check=pass\n";
    } else {
      std::cout << "result=pass\n";
    }
    return 0;
  }

  auto r = run_once(opt, opt.mode);
  print_run(r);
  if (r.result == "permission_denied") {
    return 77;
  }
  return r.result == "pass" && r.completed > 0 ? 0 : 1;
}
