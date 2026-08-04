#include "rcr_multibus/can_status_adapter.hpp"
#include "rcr_multibus/modbus_temperature_adapter.hpp"
#include "rcr_multibus/observation.hpp"

#include "rcr/can_bus.hpp"
#include "rcr/epoll_reactor.hpp"
#include "rcr/owned_fd.hpp"
#include "rcr/time.hpp"
#include "rcr_mbus/client.hpp"

#include <sys/epoll.h>

#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

struct Config {
  std::string can_interface{"vcan0"};
  std::uint8_t node_id{1};
  std::string modbus_host{"127.0.0.1"};
  std::uint16_t modbus_port{1502};
  std::uint16_t temperature_register{0};
  std::chrono::milliseconds modbus_period{100};
  std::chrono::milliseconds print_period{200};
  std::chrono::milliseconds duration{3000};
};

void usage(const char* argv0) {
  std::cerr
      << "usage: " << argv0 << " [options]\n"
      << "  --can IFACE                    SocketCAN interface (default vcan0)\n"
      << "  --node-id N                    CAN V1 node 1..31 (default 1)\n"
      << "  --modbus-host IPV4             Modbus TCP host (default 127.0.0.1)\n"
      << "  --modbus-port PORT             Modbus TCP port (default 1502)\n"
      << "  --temperature-register ADDRESS Holding register (default 0)\n"
      << "  --modbus-period-ms MS          Poll period (default 100)\n"
      << "  --print-period-ms MS           Display period (default 200)\n"
      << "  --duration-ms MS               Fixed run duration (default 3000)\n";
}

bool parse_i64(std::string_view text, std::int64_t& value) {
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  return result.ec == std::errc{} && result.ptr == end;
}

bool take_value(int argc, char** argv, int& index, std::string_view& value) {
  if (index + 1 >= argc) {
    return false;
  }
  value = argv[++index];
  return true;
}

bool parse_config(int argc, char** argv, Config& cfg) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    std::string_view value;
    if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      return false;
    }
    if (arg == "--can") {
      if (!take_value(argc, argv, i, value) || value.empty()) {
        return false;
      }
      cfg.can_interface = value;
      continue;
    }
    if (arg == "--modbus-host") {
      if (!take_value(argc, argv, i, value) || value.empty()) {
        return false;
      }
      cfg.modbus_host = value;
      continue;
    }

    if (!take_value(argc, argv, i, value)) {
      return false;
    }
    std::int64_t number = 0;
    if (!parse_i64(value, number)) {
      return false;
    }
    if (arg == "--node-id" && number >= 1 && number <= 31) {
      cfg.node_id = static_cast<std::uint8_t>(number);
    } else if (arg == "--modbus-port" && number >= 1 && number <= 65535) {
      cfg.modbus_port = static_cast<std::uint16_t>(number);
    } else if (arg == "--temperature-register" && number >= 0 && number <= 65535) {
      cfg.temperature_register = static_cast<std::uint16_t>(number);
    } else if (arg == "--modbus-period-ms" && number >= 10 && number <= 60'000) {
      cfg.modbus_period = std::chrono::milliseconds{number};
    } else if (arg == "--print-period-ms" && number >= 10 && number <= 60'000) {
      cfg.print_period = std::chrono::milliseconds{number};
    } else if (arg == "--duration-ms" && number >= 10 && number <= 86'400'000) {
      cfg.duration = std::chrono::milliseconds{number};
    } else {
      return false;
    }
  }
  return true;
}

std::int64_t monotonic_or_zero() {
  const auto now = rcr::monotonic_now_ns();
  return now ? now.value() : 0;
}

void run_can_worker(const Config& cfg, rcr::multibus::ObservationStore& store,
                    rcr::EventFd& stop_event, std::atomic<bool>& stop_requested) {
  // CAN fd、epoll 实例都只在这个线程里使用；main 仅通过 eventfd 请求退出。
  rcr::SocketCan can{cfg.can_interface};
  if (const auto opened = can.open(); !opened) {
    store.mark_can_failure(monotonic_or_zero(), opened.error().message());
    return;
  }
  if (const auto nonblocking = can.set_nonblocking(true); !nonblocking) {
    store.mark_can_failure(monotonic_or_zero(), nonblocking.error().message());
    return;
  }

  rcr::EpollReactor reactor;
  if (!reactor.valid()) {
    store.mark_can_failure(monotonic_or_zero(), "epoll_create failed");
    return;
  }
  if (const auto added = reactor.add(can.native_handle(), EPOLLIN | EPOLLERR | EPOLLHUP);
      !added) {
    store.mark_can_failure(monotonic_or_zero(), added.error().message());
    return;
  }
  if (const auto added = reactor.add(stop_event.native_handle(), EPOLLIN); !added) {
    (void)reactor.remove(can.native_handle());
    store.mark_can_failure(monotonic_or_zero(), added.error().message());
    return;
  }

  while (!stop_requested.load(std::memory_order_acquire)) {
    const auto ready = reactor.wait(std::chrono::milliseconds{-1}, 8);
    if (!ready) {
      store.mark_can_failure(monotonic_or_zero(), ready.error().message());
      break;
    }

    bool stop_ready = false;
    bool source_failed = false;
    for (const auto& event : ready.value()) {
      if (event.fd == stop_event.native_handle()) {
        stop_ready = true;
        break;
      }
    }
    if (stop_ready) {
      (void)stop_event.drain();
      break;
    }

    for (const auto& event : ready.value()) {
      if (event.fd != can.native_handle()) {
        continue;
      }
      if ((event.events & (EPOLLERR | EPOLLHUP)) != 0u) {
        store.mark_can_failure(monotonic_or_zero(), "SocketCAN epoll error/hangup");
        source_failed = true;
        break;
      }
      if ((event.events & EPOLLIN) == 0u) {
        continue;
      }

      // level-triggered + nonblocking：有界排空，防止 CAN 洪泛让退出事件长期得不到处理。
      for (std::size_t budget = 64; budget > 0; --budget) {
        const auto frame = can.receive(std::chrono::milliseconds{-1});
        if (!frame) {
          if (frame.error().code() == rcr::Errc::WouldBlock) {
            break;
          }
          store.mark_can_failure(monotonic_or_zero(), frame.error().message());
          source_failed = true;
          break;
        }
        (void)rcr::multibus::ingest_can_frame(store, frame.value(), cfg.node_id,
                                              monotonic_or_zero());
      }
    }
    if (source_failed) {
      // 单个来源失败只结束自己的 worker；辅助/旁路来源继续运行并各自报告健康。
      break;
    }
  }

  // EpollReactor 不拥有业务 fd；先取消监视，再由 SocketCan 析构关闭。
  (void)reactor.remove(stop_event.native_handle());
  (void)reactor.remove(can.native_handle());
}

void run_modbus_worker(const Config& cfg, rcr::multibus::ObservationStore& store,
                       std::atomic<bool>& stop_requested, std::mutex& wait_mutex,
                       std::condition_variable& wait_cv) {
  rcr::mbus::ClientConfig client_cfg;
  client_cfg.host = cfg.modbus_host;
  client_cfg.port = cfg.modbus_port;
  client_cfg.connect_timeout = 150ms;
  client_cfg.response_timeout = 150ms;
  // 停止上界必须可解释：一次连接尝试，不在辅助传感器线程里做长时间重试风暴。
  client_cfg.reconnect_attempts = 1;
  client_cfg.reconnect_base = 10ms;
  client_cfg.reconnect_max = 10ms;
  rcr::mbus::Client client{client_cfg};

  auto next = std::chrono::steady_clock::now();
  while (!stop_requested.load(std::memory_order_acquire)) {
    const auto values = client.read_holding(cfg.temperature_register, 1);
    const std::int64_t sampled_ns = monotonic_or_zero();
    if (!values) {
      store.mark_modbus_failure(
          sampled_ns, std::string{rcr::mbus::to_string(values.error)} + ": " + values.message);
    } else if (values.value.size() != 1) {
      store.mark_modbus_failure(sampled_ns, "read_holding returned unexpected quantity");
    } else {
      store.publish_temperature(rcr::multibus::decode_temperature_register(
          values.value.front(), cfg.temperature_register, sampled_ns));
    }

    // 使用 steady_clock 的绝对边界并跳过已经错过的轮询，避免网络过载后追赶旧事务。
    next += cfg.modbus_period;
    const auto now = std::chrono::steady_clock::now();
    while (next <= now) {
      next += cfg.modbus_period;
    }
    std::unique_lock lock(wait_mutex);
    wait_cv.wait_until(lock, next, [&stop_requested] {
      return stop_requested.load(std::memory_order_acquire);
    });
  }
}

std::int64_t age_ms(std::int64_t sampled_ns, std::int64_t now_ns) {
  if (sampled_ns <= 0 || now_ns < sampled_ns) {
    return -1;
  }
  return (now_ns - sampled_ns) / 1'000'000;
}

void print_snapshot(const Config& cfg, const rcr::multibus::ObservationSnapshot& snapshot,
                    std::int64_t now_ns) {
  const std::int64_t can_age = age_ms(snapshot.can.sampled_ns, now_ns);
  const std::int64_t modbus_age = age_ms(snapshot.temperature.sampled_ns, now_ns);
  const bool can_stale = rcr::multibus::sample_is_stale(
      snapshot.can.valid, snapshot.can.sampled_ns, now_ns, 350'000'000LL);
  const bool modbus_stale = rcr::multibus::sample_is_stale(
      snapshot.temperature.valid, snapshot.temperature.sampled_ns, now_ns,
      cfg.modbus_period.count() * 3 * 1'000'000LL);

  std::cout << "CAN: ";
  if (snapshot.can.valid) {
    std::cout << "node=" << static_cast<unsigned>(snapshot.can.node_id)
              << " input_bits=0x" << std::hex << std::setw(4) << std::setfill('0')
              << snapshot.can.input_bits << std::dec << std::setfill(' ')
              << " interlock=" << (snapshot.can.interlock_ready ? "ready" : "blocked")
              << " fault=" << snapshot.can.fault_code << " age_ms=" << can_age
              << " stale=" << (can_stale ? "yes" : "no");
  } else {
    std::cout << "no-sample";
  }
  std::cout << " source=" << rcr::multibus::to_string(snapshot.can_source.health);
  if (snapshot.can_source.health == rcr::multibus::SourceHealth::Faulted) {
    std::cout << " detail=\"" << snapshot.can_source.detail << "\"";
  }

  std::cout << " | Modbus: ";
  if (snapshot.temperature.valid) {
    std::cout << "holding[" << snapshot.temperature.register_address << "] temp="
              << std::fixed << std::setprecision(1)
              << static_cast<double>(snapshot.temperature.deci_celsius) / 10.0
              << "C age_ms=" << modbus_age << " stale=" << (modbus_stale ? "yes" : "no");
  } else {
    std::cout << "no-sample";
  }
  std::cout << " source=" << rcr::multibus::to_string(snapshot.modbus_source.health);
  if (snapshot.modbus_source.health == rcr::multibus::SourceHealth::Faulted) {
    std::cout << " detail=\"" << snapshot.modbus_source.detail << "\"";
  }
  std::cout << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      return 0;
    }
  }
  Config cfg;
  if (!parse_config(argc, argv, cfg)) {
    usage(argv[0]);
    return 2;
  }

  auto stop_event_result = rcr::EventFd::create();
  if (!stop_event_result) {
    std::cerr << "eventfd create failed: " << stop_event_result.error().message() << "\n";
    return 1;
  }
  rcr::EventFd stop_event = std::move(stop_event_result.value());

  rcr::multibus::ObservationStore store;
  std::atomic<bool> stop_requested{false};
  std::mutex wait_mutex;
  std::condition_variable wait_cv;

  std::thread can_thread(run_can_worker, std::cref(cfg), std::ref(store),
                         std::ref(stop_event), std::ref(stop_requested));
  std::thread modbus_thread(run_modbus_worker, std::cref(cfg), std::ref(store),
                            std::ref(stop_requested), std::ref(wait_mutex), std::ref(wait_cv));

  const auto deadline = std::chrono::steady_clock::now() + cfg.duration;
  while (std::chrono::steady_clock::now() < deadline) {
    print_snapshot(cfg, store.snapshot(), monotonic_or_zero());
    std::this_thread::sleep_for(cfg.print_period);
  }

  stop_requested.store(true, std::memory_order_release);
  (void)stop_event.signal_stop();
  wait_cv.notify_all();
  can_thread.join();
  modbus_thread.join();

  const auto final = store.snapshot();
  std::cout << "summary: can_updates=" << final.can_source.updates
            << " can_failures=" << final.can_source.failures
            << " modbus_updates=" << final.modbus_source.updates
            << " modbus_failures=" << final.modbus_source.failures << "\n";
  return 0;
}
