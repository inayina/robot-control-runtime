// 独立 CAN 节点模拟器：只经 SocketCAN 通信，不链接 LinuxRuntime。
// 单线程 epoll 等待 CAN、timerfd 与 signalfd；vcan 不代表物理 CAN 证据。
#include "rcr/can_bus.hpp"
#include "rcr/can_v1.hpp"
#include "rcr/epoll_reactor.hpp"
#include "rcr/node_sim.hpp"
#include "rcr/time.hpp"
#include "rcr/vcan.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace {

struct Options {
  std::string can_if{"vcan0"};
  std::uint8_t node_id{1};
  std::int64_t heartbeat_ms{100};
  std::int64_t duration_ms{0};  // 0 = 直到信号
  std::uint16_t boot_id{1};
  std::uint16_t session_id{1};
  bool interlock_ready{true};
  std::uint16_t input_bits{0};

  // 故障注入默认全关；正式协议消息不承载这些开关。
  bool fault_stop_heartbeat{false};
  std::int64_t fault_delay_response_ms{0};
  std::int64_t fault_restart_after_ms{0};
  std::int64_t fault_send_illegal_after_ms{0};
};

struct PendingCommand {
  // 仅服务默认关闭的延迟响应故障注入。receive_ns 锚定有效期，due_ns 决定何时重新评估；
  // 两者都属于 CLOCK_MONOTONIC，不能混用 wall clock。
  rcr::can_v1::WireOutputCommand command{};
  std::int64_t receive_ns{0};
  std::int64_t due_ns{0};
};

class OwnedFd {
 public:
  OwnedFd() = default;
  explicit OwnedFd(int fd) : fd_(fd) {}
  ~OwnedFd() { reset(); }

  OwnedFd(const OwnedFd&) = delete;
  OwnedFd& operator=(const OwnedFd&) = delete;

  OwnedFd(OwnedFd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  OwnedFd& operator=(OwnedFd&& other) noexcept {
    if (this != &other) {
      reset();
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

  /// 关闭当前唯一拥有的 fd；幂等。该小类只用于本 app 的 timerfd/signalfd 生命周期。
  void reset() noexcept {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  /// 放弃关闭责任并返回原 fd；当前路径很少使用，调用后必须由新拥有者负责 close。
  int release() noexcept {
    const int out = fd_;
    fd_ = -1;
    return out;
  }

 private:
  int fd_{-1};
};

bool parse_i64(std::string_view text, std::int64_t& value) {
  try {
    std::size_t used = 0;
    value = std::stoll(std::string(text), &used, 10);
    return used == text.size();
  } catch (...) {
    return false;
  }
}

bool parse_u16(std::string_view text, std::uint16_t& value) {
  std::int64_t wide = 0;
  if (!parse_i64(text, wide) || wide < 0 || wide > 65535) {
    return false;
  }
  value = static_cast<std::uint16_t>(wide);
  return true;
}

void usage(const char* program) {
  std::cerr
      << "usage: " << program
      << " [--can IFACE] [--node-id 1..31] [--heartbeat-ms N] [--duration-ms N]\n"
         "       [--boot-id N] [--session-id N] [--interlock 0|1] [--input-bits N]\n"
         "       [--fault-stop-heartbeat] [--fault-delay-response-ms N]\n"
         "       [--fault-restart-after-ms N] [--fault-send-illegal-after-ms N]\n"
         "\n"
         "Fault flags default off. duration-ms 0 runs until SIGINT/SIGTERM.\n"
         "Requires an existing CAN interface (see linux/scripts/setup_vcan.sh).\n";
}

bool parse_options(int argc, char** argv, Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--fault-stop-heartbeat") {
      options.fault_stop_heartbeat = true;
      continue;
    }
    if (arg == "--help" || arg == "-h") {
      return false;
    }
    if (i + 1 >= argc) {
      return false;
    }
    const std::string_view value(argv[++i]);
    if (arg == "--can") {
      options.can_if = std::string(value);
    } else if (arg == "--node-id") {
      std::int64_t wide = 0;
      if (!parse_i64(value, wide) || wide < 1 || wide > 31) {
        return false;
      }
      options.node_id = static_cast<std::uint8_t>(wide);
    } else if (arg == "--heartbeat-ms") {
      if (!parse_i64(value, options.heartbeat_ms) || options.heartbeat_ms < 1 ||
          options.heartbeat_ms > 60'000) {
        return false;
      }
    } else if (arg == "--duration-ms") {
      if (!parse_i64(value, options.duration_ms) || options.duration_ms < 0 ||
          options.duration_ms > 86'400'000) {
        return false;
      }
    } else if (arg == "--boot-id") {
      if (!parse_u16(value, options.boot_id) || options.boot_id == 0) {
        return false;
      }
    } else if (arg == "--session-id") {
      if (!parse_u16(value, options.session_id) || options.session_id == 0) {
        return false;
      }
    } else if (arg == "--interlock") {
      std::int64_t wide = 0;
      if (!parse_i64(value, wide) || (wide != 0 && wide != 1)) {
        return false;
      }
      options.interlock_ready = wide == 1;
    } else if (arg == "--input-bits") {
      if (!parse_u16(value, options.input_bits)) {
        return false;
      }
    } else if (arg == "--fault-delay-response-ms") {
      if (!parse_i64(value, options.fault_delay_response_ms) ||
          options.fault_delay_response_ms < 0 ||
          options.fault_delay_response_ms > 60'000) {
        return false;
      }
    } else if (arg == "--fault-restart-after-ms") {
      if (!parse_i64(value, options.fault_restart_after_ms) ||
          options.fault_restart_after_ms < 0 ||
          options.fault_restart_after_ms > 86'400'000) {
        return false;
      }
    } else if (arg == "--fault-send-illegal-after-ms") {
      if (!parse_i64(value, options.fault_send_illegal_after_ms) ||
          options.fault_send_illegal_after_ms < 0 ||
          options.fault_send_illegal_after_ms > 86'400'000) {
        return false;
      }
    } else {
      return false;
    }
  }
  return true;
}

OwnedFd make_periodic_timerfd(std::int64_t interval_ms) {
  // timerfd 把“时间到”转换成普通可读 fd，使 heartbeat 与 CAN/信号在同一 epoll 线程排序。
  // TFD_NONBLOCK 保证 readiness 与 read 之间即使出现变化也不会卡死事件循环。
  const int fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  if (fd < 0) {
    return OwnedFd{};
  }
  itimerspec spec{};
  spec.it_interval.tv_sec = interval_ms / 1000;
  spec.it_interval.tv_nsec = (interval_ms % 1000) * 1'000'000L;
  spec.it_value = spec.it_interval;
  if (::timerfd_settime(fd, 0, &spec, nullptr) != 0) {
    ::close(fd);
    return OwnedFd{};
  }
  return OwnedFd{fd};
}

OwnedFd make_oneshot_timerfd(std::int64_t delay_ms) {
  if (delay_ms <= 0) {
    return OwnedFd{};
  }
  const int fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  if (fd < 0) {
    return OwnedFd{};
  }
  itimerspec spec{};
  spec.it_value.tv_sec = delay_ms / 1000;
  spec.it_value.tv_nsec = (delay_ms % 1000) * 1'000'000L;
  if (::timerfd_settime(fd, 0, &spec, nullptr) != 0) {
    ::close(fd);
    return OwnedFd{};
  }
  return OwnedFd{fd};
}

bool arm_oneshot_timerfd(int fd, std::int64_t delay_ms) {
  // it_value=0 按 timerfd 合同表示 disarm；非零值从当前 CLOCK_MONOTONIC 起相对计时。
  itimerspec spec{};
  if (delay_ms > 0) {
    spec.it_value.tv_sec = delay_ms / 1000;
    spec.it_value.tv_nsec = (delay_ms % 1000) * 1'000'000L;
  }
  return ::timerfd_settime(fd, 0, &spec, nullptr) == 0;
}

OwnedFd make_signalfd() {
  sigset_t mask;
  ::sigemptyset(&mask);
  ::sigaddset(&mask, SIGINT);
  ::sigaddset(&mask, SIGTERM);
  // 先在线程信号掩码中屏蔽，再转成 fd，避免默认 handler 与 epoll 路径竞态。
  // 本程序在创建额外线程前完成这一步；未来多线程程序需确保相关线程继承/设置一致 mask。
  if (::sigprocmask(SIG_BLOCK, &mask, nullptr) != 0) {
    return OwnedFd{};
  }
  const int fd = ::signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  return OwnedFd{fd};
}

bool consume_timerfd(int fd) {
  // timerfd 必须 read 8-byte 计数才能清除 readiness；计数可能 >1，表示事件循环落后并
  // 合并了多次到期。模拟器只需发送当前 heartbeat，不补发所有历史周期。
  std::uint64_t expirations = 0;
  const ssize_t n = ::read(fd, &expirations, sizeof(expirations));
  return n == static_cast<ssize_t>(sizeof(expirations));
}

bool consume_signalfd(int fd) {
  signalfd_siginfo info{};
  const ssize_t n = ::read(fd, &info, sizeof(info));
  return n == static_cast<ssize_t>(sizeof(info));
}

rcr::Result<void> send_wire(rcr::SocketCan& bus, const rcr::CanFrame& frame) {
  return bus.send(frame);
}

void send_status(rcr::SocketCan& bus, const rcr::can_v1::WireOutputStatus& status) {
  // 模拟器当前把发送失败留给进程级验收（无重试队列），避免测试节点悄悄积压旧状态。
  // 可部署 daemon 必须把 WouldBlock/IoError 做成可观测故障，不能照搬这里的忽略策略。
  const auto encoded = rcr::can_v1::encode_output_status(status);
  if (encoded) {
    (void)send_wire(bus, encoded.value());
  }
}

void arm_deadline_timer(int deadline_timer_fd,
                        const std::vector<PendingCommand>& pending,
                        const rcr::CanNodeLogic& node, std::int64_t now_ns) {
  if (pending.empty() && !node.output_lease_active()) {
    (void)arm_oneshot_timerfd(deadline_timer_fd, 0);
    return;
  }

  // 一个 timerfd 同时服务全部延迟命令与普通输出 lease，每次只对准最早 deadline；无需
  // 为每条命令或每次输出创建 fd。两类时间都属于 CLOCK_MONOTONIC。
  std::int64_t min_due = node.output_lease_active()
                             ? node.output_lease_deadline_ns()
                             : pending.front().due_ns;
  for (const auto& entry : pending) {
    if (entry.due_ns < min_due) {
      min_due = entry.due_ns;
    }
  }
  // timerfd 接受纳秒，但故障参数以毫秒表达；这里向上取整，避免因截断而早于 due 触发。
  const std::int64_t delay_ms =
      (min_due > now_ns) ? ((min_due - now_ns + 999'999) / 1'000'000) : 1;
  (void)arm_oneshot_timerfd(deadline_timer_fd, delay_ms);
}

void flush_pending_commands(rcr::SocketCan& bus, rcr::CanNodeLogic& node,
                            std::vector<PendingCommand>& pending,
                            int deadline_timer_fd,
                            std::int64_t now_ns) {
  (void)node.expire_output_lease(now_ns);
  // 到期项按当前 now 应用，未到期项搬到新容器。该队列只属于故障注入，默认关闭且规模
  // 受验收流量限制；正式输入事件队列需要独立容量上限和溢出故障合同。
  std::vector<PendingCommand> remain;
  remain.reserve(pending.size());
  for (const auto& item : pending) {
    if (item.due_ns <= now_ns) {
      const auto handled =
          node.apply_command(item.command, item.receive_ns, now_ns);
      if (handled.send_status) {
        send_status(bus, handled.status);
      }
    } else {
      remain.push_back(item);
    }
  }
  pending.swap(remain);
  arm_deadline_timer(deadline_timer_fd, pending, node, now_ns);
}

void handle_command_now_or_later(rcr::SocketCan& bus, rcr::CanNodeLogic& node,
                                 Options& options, std::vector<PendingCommand>& pending,
                                 int deadline_timer_fd,
                                 const rcr::can_v1::WireOutputCommand& command,
                                 std::int64_t receive_ns) {
  if (options.fault_delay_response_ms <= 0) {
    const auto handled = node.apply_command(command, receive_ns, receive_ns);
    if (handled.send_status) {
      send_status(bus, handled.status);
    }
    arm_deadline_timer(deadline_timer_fd, pending, node, receive_ns);
    return;
  }

  PendingCommand item{};
  item.command = command;
  item.receive_ns = receive_ns;
  item.due_ns = receive_ns + options.fault_delay_response_ms * 1'000'000LL;
  pending.push_back(item);
  arm_deadline_timer(deadline_timer_fd, pending, node, receive_ns);
}

void drain_can(rcr::SocketCan& bus, rcr::CanNodeLogic& node, Options& options,
               std::vector<PendingCommand>& pending, int deadline_timer_fd) {
  for (;;) {
    // epoll 是 level-triggered，必须读到当前队列为空，才能避免同一批帧反复唤醒。
    // receive(0) 在非阻塞 socket 上通常以 WouldBlock 结束；当前模拟器对其他 read error
    // 也结束本轮 drain，生产 daemon 需进一步区分并升级 IoError。
    auto frame = bus.receive(std::chrono::milliseconds{0});
    if (!frame) {
      break;
    }
    const auto now = rcr::monotonic_now_ns();
    if (!now) {
      break;
    }

    const std::uint32_t raw11 = frame.value().can_id & 0x7FFu;
    const auto function = rcr::can_v1::function_from_can_id(raw11);
    const auto dest = rcr::can_v1::node_id_from_can_id(raw11);
    if (function != static_cast<std::uint8_t>(rcr::can_v1::Function::OutputCommand) ||
        dest != node.node_id()) {
      continue;
    }

    const auto decoded = rcr::can_v1::decode_output_command(frame.value());
    if (!decoded) {
      // 与 CanNodeLogic::on_frame 一致：非法命令计拒绝且无 OutputStatus。
      (void)node.on_frame(frame.value(), now.value());
      continue;
    }
    handle_command_now_or_later(bus, node, options, pending, deadline_timer_fd,
                                decoded.value(), now.value());
  }
}

}  // namespace

int main(int argc, char** argv) {
  Options options{};
  if (!parse_options(argc, argv, options)) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (!rcr::can_interface_available(options.can_if)) {
    const auto status = rcr::probe_can_interface(options.can_if);
    std::cerr << "error: CAN interface '" << options.can_if << "' unavailable (";
    switch (status) {
      case rcr::CanInterfaceStatus::Missing:
        std::cerr << "missing";
        break;
      case rcr::CanInterfaceStatus::NotCan:
        std::cerr << "not CAN";
        break;
      case rcr::CanInterfaceStatus::InvalidName:
        std::cerr << "invalid name";
        break;
      case rcr::CanInterfaceStatus::Available:
        std::cerr << "unknown";
        break;
    }
    std::cerr << "); run: sudo ./linux/scripts/setup_vcan.sh " << options.can_if
              << "\n";
    return EXIT_FAILURE;
  }

  // 业务状态与 Linux fd 生命周期分离：CanNodeLogic 不知道 socket/timer，便于无内核单测。
  rcr::CanNodeLogic::Config logic_config{};
  logic_config.node_id = options.node_id;
  logic_config.boot_id = options.boot_id;
  logic_config.session_id = options.session_id;
  logic_config.interlock_ready = options.interlock_ready;
  logic_config.input_bits = options.input_bits;
  rcr::CanNodeLogic node(logic_config);

  rcr::SocketCan bus(options.can_if);
  if (auto rc = bus.open(); !rc) {
    std::cerr << "error: open " << options.can_if << ": " << rc.error().message()
              << "\n";
    return EXIT_FAILURE;
  }
  if (auto rc = bus.set_nonblocking(true); !rc) {
    std::cerr << "error: set_nonblocking: " << rc.error().message() << "\n";
    return EXIT_FAILURE;
  }

  // 每个外部刺激都用一个明确 fd 表达：信号、周期 heartbeat、总运行时长、软重启、
  // 非法帧注入和延迟命令到期。全部由同一线程 epoll 等待，没有隐藏 callback 线程。
  OwnedFd signal_fd = make_signalfd();
  OwnedFd heartbeat_fd = make_periodic_timerfd(options.heartbeat_ms);
  OwnedFd duration_fd = make_oneshot_timerfd(options.duration_ms);
  OwnedFd restart_fd = make_oneshot_timerfd(options.fault_restart_after_ms);
  OwnedFd illegal_fd = make_oneshot_timerfd(options.fault_send_illegal_after_ms);
  OwnedFd delay_fd{::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK)};

  if (!signal_fd.valid() || !heartbeat_fd.valid() || !delay_fd.valid()) {
    std::cerr << "error: failed to create timerfd/signalfd\n";
    return EXIT_FAILURE;
  }
  if (options.duration_ms > 0 && !duration_fd.valid()) {
    std::cerr << "error: failed to create duration timerfd\n";
    return EXIT_FAILURE;
  }
  if (options.fault_restart_after_ms > 0 && !restart_fd.valid()) {
    std::cerr << "error: failed to create restart timerfd\n";
    return EXIT_FAILURE;
  }
  if (options.fault_send_illegal_after_ms > 0 && !illegal_fd.valid()) {
    std::cerr << "error: failed to create illegal-frame timerfd\n";
    return EXIT_FAILURE;
  }

  rcr::EpollReactor reactor;
  if (!reactor.valid()) {
    std::cerr << "error: epoll_create failed\n";
    return EXIT_FAILURE;
  }

  // reactor 只借用这些 fd，不拥有它们。注册完成后 OwnedFd/SocketCan 必须活到 remove。
  const int can_fd = bus.native_handle();
  if (auto rc = reactor.add(can_fd, EPOLLIN); !rc) {
    std::cerr << "error: epoll add can: " << rc.error().message() << "\n";
    return EXIT_FAILURE;
  }
  if (auto rc = reactor.add(signal_fd.get(), EPOLLIN); !rc) {
    std::cerr << "error: epoll add signalfd: " << rc.error().message() << "\n";
    return EXIT_FAILURE;
  }
  if (auto rc = reactor.add(heartbeat_fd.get(), EPOLLIN); !rc) {
    std::cerr << "error: epoll add heartbeat timer: " << rc.error().message()
              << "\n";
    return EXIT_FAILURE;
  }
  if (auto rc = reactor.add(delay_fd.get(), EPOLLIN); !rc) {
    std::cerr << "error: epoll add delay timer: " << rc.error().message() << "\n";
    return EXIT_FAILURE;
  }
  if (duration_fd.valid()) {
    if (auto rc = reactor.add(duration_fd.get(), EPOLLIN); !rc) {
      std::cerr << "error: epoll add duration timer: " << rc.error().message()
                << "\n";
      return EXIT_FAILURE;
    }
  }
  if (restart_fd.valid()) {
    if (auto rc = reactor.add(restart_fd.get(), EPOLLIN); !rc) {
      std::cerr << "error: epoll add restart timer: " << rc.error().message()
                << "\n";
      return EXIT_FAILURE;
    }
  }
  if (illegal_fd.valid()) {
    if (auto rc = reactor.add(illegal_fd.get(), EPOLLIN); !rc) {
      std::cerr << "error: epoll add illegal timer: " << rc.error().message()
                << "\n";
      return EXIT_FAILURE;
    }
  }

  std::cout << "rcr_node_sim: can=" << options.can_if
            << " node=" << static_cast<int>(options.node_id)
            << " session=" << node.session_id() << " boot=" << node.boot_id()
            << " hb_ms=" << options.heartbeat_ms
            << " duration_ms=" << options.duration_ms
            << " stop_hb=" << (options.fault_stop_heartbeat ? 1 : 0)
            << " delay_ms=" << options.fault_delay_response_ms
            << " restart_ms=" << options.fault_restart_after_ms
            << " illegal_ms=" << options.fault_send_illegal_after_ms << "\n";

  std::vector<PendingCommand> pending;
  bool running = true;
  std::uint64_t heartbeat_sends = 0;

  while (running) {
    // 无限等待不会妨碍退出，因为 SIGINT/SIGTERM 已通过 signalfd 成为一个 readiness 事件。
    const auto ready = reactor.wait(std::chrono::milliseconds{-1});
    if (!ready) {
      std::cerr << "error: epoll_wait: " << ready.error().message() << "\n";
      break;
    }

    for (const auto& event : ready.value()) {
      // 这里按 fd 身份串行处理。同一轮可能多个 fd ready；设置 running=false 后 break，
      // 让关闭路径统一 remove/close，而不是在分支中部分销毁资源。
      if (event.fd == signal_fd.get()) {
        (void)consume_signalfd(signal_fd.get());
        running = false;
        break;
      }
      if (duration_fd.valid() && event.fd == duration_fd.get()) {
        (void)consume_timerfd(duration_fd.get());
        running = false;
        break;
      }
      if (event.fd == heartbeat_fd.get()) {
        (void)consume_timerfd(heartbeat_fd.get());
        if (!options.fault_stop_heartbeat) {
          const auto hb = rcr::can_v1::encode_heartbeat(node.make_heartbeat());
          const auto st = rcr::can_v1::encode_node_status(node.make_status());
          if (hb) {
            (void)send_wire(bus, hb.value());
            ++heartbeat_sends;
          }
          if (st) {
            (void)send_wire(bus, st.value());
          }
        }
        // heartbeat tick 也顺便 flush pending，避免 delay timer 舍入或合并时命令长期滞留。
        const auto now = rcr::monotonic_now_ns();
        if (now) {
          flush_pending_commands(bus, node, pending, delay_fd.get(), now.value());
        }
      }
      if (event.fd == delay_fd.get()) {
        (void)consume_timerfd(delay_fd.get());
        const auto now = rcr::monotonic_now_ns();
        if (now) {
          flush_pending_commands(bus, node, pending, delay_fd.get(), now.value());
        }
      }
      if (restart_fd.valid() && event.fd == restart_fd.get()) {
        (void)consume_timerfd(restart_fd.get());
        node.soft_restart();
        const auto now = rcr::monotonic_now_ns();
        if (now) {
          arm_deadline_timer(delay_fd.get(), pending, node, now.value());
        }
        std::cout << "rcr_node_sim: soft_restart boot=" << node.boot_id()
                  << " session=" << node.session_id() << "\n";
        // oneshot 完成后先从 epoll 删除，再关闭 OwnedFd，避免 fd 数字复用后旧注册产生歧义。
        (void)reactor.remove(restart_fd.get());
        restart_fd.reset();
      }
      if (illegal_fd.valid() && event.fd == illegal_fd.get()) {
        (void)consume_timerfd(illegal_fd.get());
        // 故意发送错误版本 heartbeat，供对端验证协议拒绝；默认关闭。
        rcr::CanFrame bad{};
        bad.can_id = rcr::can_v1::make_can_id(rcr::can_v1::Function::Heartbeat,
                                              options.node_id);
        bad.len = 8;
        bad.data[0] = 0x02;  // unsupported version
        bad.data[2] = 0x00;
        bad.data[3] = 0x01;
        bad.data[4] = 0x00;
        bad.data[5] = 0x01;
        (void)send_wire(bus, bad);
        std::cout << "rcr_node_sim: injected illegal heartbeat frame\n";
        (void)reactor.remove(illegal_fd.get());
        illegal_fd.reset();
      }
      if (event.fd == can_fd) {
        drain_can(bus, node, options, pending, delay_fd.get());
      }
    }
  }

  // 有界关闭：先从 epoll interest list 摘掉所有非 owning fd，再由各拥有者 close。
  // reactor 本身最后析构；即便忽略 remove 错误，也不会把业务 fd 的关闭责任转给 epoll。
  (void)reactor.remove(can_fd);
  (void)reactor.remove(signal_fd.get());
  (void)reactor.remove(heartbeat_fd.get());
  (void)reactor.remove(delay_fd.get());
  if (duration_fd.valid()) {
    (void)reactor.remove(duration_fd.get());
  }
  if (restart_fd.valid()) {
    (void)reactor.remove(restart_fd.get());
  }
  if (illegal_fd.valid()) {
    (void)reactor.remove(illegal_fd.get());
  }

  bus.close();
  delay_fd.reset();
  heartbeat_fd.reset();
  signal_fd.reset();
  duration_fd.reset();
  restart_fd.reset();
  illegal_fd.reset();

  std::cout << "rcr_node_sim: exit heartbeats=" << heartbeat_sends
            << " protocol_rejects=" << node.protocol_rejects()
            << " output=0x" << std::hex
            << static_cast<unsigned>(node.output_bits()) << std::dec << "\n";
  return EXIT_SUCCESS;
}
