// 双进程 vcan 验收：本进程与 rcr_node_sim 只经 SocketCAN 通信。
// 缺少 CAN 接口时必须失败（不是 Skip）。vcan 证据不等于物理 CAN。
#include "rcr/can_bus.hpp"
#include "rcr/can_v1.hpp"
#include "rcr/time.hpp"
#include "rcr/vcan.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

struct Options {
  std::string can_if{"vcan0"};
  std::uint8_t node_id{1};
  std::string sim_path{};
  std::string evidence_path{};
};

struct ScenarioResult {
  std::string name;
  bool passed{false};
  std::string detail;
};

class ChildProcess {
 public:
  // 验收程序拥有模拟器子进程；析构时确保不会把后台进程遗留给下一场景。
  ~ChildProcess() { stop(); }

  bool start(const std::string& path, const std::vector<std::string>& args,
             const std::string& log_path) {
    // 每个场景先收掉旧子进程，再创建独立日志，保证场景之间不共享模拟器状态/session。
    stop();
    log_path_ = log_path;
    const int log_fd =
        ::open(log_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
    if (log_fd < 0) {
      return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
      ::close(log_fd);
      return false;
    }
    if (pid == 0) {
      // fork 后子进程先把 stdout/stderr 重定向到场景日志，再 exec 成真正模拟器。
      // exec 成功不会返回；失败使用 _Exit，避免执行父进程 C++ 栈清理或重复 flush 缓冲区。
      ::dup2(log_fd, STDOUT_FILENO);
      ::dup2(log_fd, STDERR_FILENO);
      ::close(log_fd);
      std::vector<char*> argv;
      argv.push_back(const_cast<char*>(path.c_str()));
      for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
      }
      argv.push_back(nullptr);
      ::execv(path.c_str(), argv.data());
      std::perror("execv rcr_node_sim");
      std::_Exit(127);
    }

    ::close(log_fd);
    pid_ = pid;
    return true;
  }

  void stop() {
    if (pid_ <= 0) {
      return;
    }
    // 先给模拟器最多约 1s 走 signalfd 有界关闭；只有不收敛才 SIGKILL。waitpid 同时回收
    // zombie，pid_=-1 后析构重复 stop 是幂等的。
    ::kill(pid_, SIGTERM);
    int status = 0;
    for (int i = 0; i < 50; ++i) {
      const pid_t rc = ::waitpid(pid_, &status, WNOHANG);
      if (rc == pid_) {
        pid_ = -1;
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    ::kill(pid_, SIGKILL);
    ::waitpid(pid_, &status, 0);
    pid_ = -1;
  }

  [[nodiscard]] bool running() const {
    if (pid_ <= 0) {
      return false;
    }
    int status = 0;
    const pid_t rc = ::waitpid(pid_, &status, WNOHANG);
    if (rc == pid_) {
      return false;
    }
    return true;
  }

  [[nodiscard]] const std::string& log_path() const { return log_path_; }

 private:
  pid_t pid_{-1};
  std::string log_path_{};
};

std::string dirname_of(std::string_view path) {
  const auto pos = path.find_last_of('/');
  if (pos == std::string_view::npos) {
    return ".";
  }
  if (pos == 0) {
    return "/";
  }
  return std::string(path.substr(0, pos));
}

std::string default_sim_path() {
  char self[PATH_MAX];
  const ssize_t n = ::readlink("/proc/self/exe", self, sizeof(self) - 1);
  if (n <= 0) {
    return "./rcr_node_sim";
  }
  self[n] = '\0';
  return dirname_of(self) + "/rcr_node_sim";
}

bool parse_options(int argc, char** argv, Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
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
      try {
        const int id = std::stoi(std::string(value));
        if (id < 1 || id > 31) {
          return false;
        }
        options.node_id = static_cast<std::uint8_t>(id);
      } catch (...) {
        return false;
      }
    } else if (arg == "--sim-path") {
      options.sim_path = std::string(value);
    } else if (arg == "--evidence") {
      options.evidence_path = std::string(value);
    } else {
      return false;
    }
  }
  if (options.sim_path.empty()) {
    options.sim_path = default_sim_path();
  }
  return true;
}

void usage(const char* program) {
  std::cerr << "usage: " << program
            << " [--can IFACE] [--node-id N] [--sim-path PATH] [--evidence FILE]\n"
               "Requires an existing CAN interface. Missing interface fails hard.\n";
}

std::optional<rcr::CanFrame> recv_one(rcr::SocketCan& bus,
                                      std::chrono::milliseconds timeout) {
  auto frame = bus.receive(timeout);
  if (!frame) {
    return std::nullopt;
  }
  return frame.value();
}

template <typename Pred>
std::optional<rcr::CanFrame> recv_until(rcr::SocketCan& bus, Pred pred,
                                        std::chrono::milliseconds budget) {
  // 使用 steady_clock 建立整个场景预算，内部每次最多阻塞 50ms。这样无关 CAN 流量会被
  // 跳过，但不会因每帧重新开始完整 timeout 而无限延长验收。
  const auto deadline = std::chrono::steady_clock::now() + budget;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    const auto slice =
        left.count() > 50 ? std::chrono::milliseconds{50} : left;
    if (slice.count() <= 0) {
      break;
    }
    auto frame = recv_one(bus, slice);
    if (!frame) {
      continue;
    }
    if (pred(*frame)) {
      return frame;
    }
  }
  return std::nullopt;
}

bool is_node_fn(const rcr::CanFrame& frame, rcr::can_v1::Function fn,
                std::uint8_t node) {
  if (frame.is_extended() || frame.is_rtr() || frame.len != 8) {
    return false;
  }
  const auto raw = frame.can_id & 0x7FFu;
  return rcr::can_v1::function_from_can_id(raw) ==
             static_cast<std::uint8_t>(fn) &&
         rcr::can_v1::node_id_from_can_id(raw) == node;
}

rcr::Result<void> send_command(rcr::SocketCan& bus, std::uint8_t node,
                               std::uint8_t mask, std::uint16_t session,
                               std::uint16_t sequence, std::uint8_t values,
                               std::uint8_t validity_10ms) {
  rcr::can_v1::WireOutputCommand cmd{};
  cmd.node_id = node;
  cmd.mask = mask;
  cmd.session_id = session;
  cmd.sequence = sequence;
  cmd.values = values;
  cmd.validity_10ms = validity_10ms;
  auto encoded = rcr::can_v1::encode_output_command(cmd);
  if (!encoded) {
    return encoded.error();
  }
  return bus.send(encoded.value());
}

std::optional<rcr::can_v1::WireOutputStatus> wait_status(
    rcr::SocketCan& bus, std::uint8_t node, std::uint16_t sequence,
    std::chrono::milliseconds budget) {
  auto frame = recv_until(
      bus,
      [&](const rcr::CanFrame& f) {
        if (!is_node_fn(f, rcr::can_v1::Function::OutputStatus, node)) {
          return false;
        }
        const auto decoded = rcr::can_v1::decode_output_status(f);
        return decoded && decoded.value().sequence == sequence;
      },
      budget);
  if (!frame) {
    return std::nullopt;
  }
  auto decoded = rcr::can_v1::decode_output_status(*frame);
  if (!decoded) {
    return std::nullopt;
  }
  return decoded.value();
}

std::optional<rcr::can_v1::WireHeartbeat> wait_heartbeat(
    rcr::SocketCan& bus, std::uint8_t node, std::chrono::milliseconds budget) {
  auto frame = recv_until(
      bus,
      [&](const rcr::CanFrame& f) {
        return is_node_fn(f, rcr::can_v1::Function::Heartbeat, node) &&
               rcr::can_v1::decode_heartbeat(f).ok();
      },
      budget);
  if (!frame) {
    return std::nullopt;
  }
  return rcr::can_v1::decode_heartbeat(*frame).value();
}

std::optional<rcr::can_v1::WireNodeStatus> wait_node_status(
    rcr::SocketCan& bus, std::uint8_t node, std::chrono::milliseconds budget) {
  auto frame = recv_until(
      bus,
      [&](const rcr::CanFrame& f) {
        return is_node_fn(f, rcr::can_v1::Function::Status, node) &&
               rcr::can_v1::decode_node_status(f).ok();
      },
      budget);
  if (!frame) {
    return std::nullopt;
  }
  return rcr::can_v1::decode_node_status(*frame).value();
}

ScenarioResult scenario_heartbeat_status(rcr::SocketCan& bus, ChildProcess& child,
                                         const Options& options) {
  // 最小单向路径：独立子进程 → 内核 vcan → 本进程，验证 heartbeat/status 和 session。
  ScenarioResult out{"heartbeat_status", false, {}};
  std::vector<std::string> args{
      "--can",       options.can_if,
      "--node-id",   std::to_string(options.node_id),
      "--heartbeat-ms", "50",
      "--duration-ms",  "8000",
      "--boot-id",   "1",
      "--session-id", "1",
  };
  if (!child.start(options.sim_path, args, "/tmp/rcr_vcan_acc_hb.log")) {
    out.detail = "failed to start simulator";
    return out;
  }
  const auto hb = wait_heartbeat(bus, options.node_id, std::chrono::milliseconds{1000});
  const auto st =
      wait_node_status(bus, options.node_id, std::chrono::milliseconds{1000});
  child.stop();
  if (!hb || !st) {
    out.detail = "missing heartbeat or status";
    return out;
  }
  if (hb->session_id != 1 || st->session_id != 1) {
    out.detail = "unexpected session";
    return out;
  }
  out.passed = true;
  out.detail = "hb_seq=" + std::to_string(hb->hb_seq);
  return out;
}

ScenarioResult scenario_command_loop(rcr::SocketCan& bus, ChildProcess& child,
                                     const Options& options) {
  // 最小双向路径：命令经 vcan 到 Node，OutputStatus 再返回；不共享 CanNodeLogic 内存。
  ScenarioResult out{"command_loopback", false, {}};
  std::vector<std::string> args{
      "--can", options.can_if, "--node-id", std::to_string(options.node_id),
      "--heartbeat-ms", "50", "--duration-ms", "8000", "--session-id", "10",
  };
  if (!child.start(options.sim_path, args, "/tmp/rcr_vcan_acc_cmd.log")) {
    out.detail = "failed to start simulator";
    return out;
  }
  if (!wait_heartbeat(bus, options.node_id, std::chrono::milliseconds{1000})) {
    child.stop();
    out.detail = "no heartbeat before command";
    return out;
  }
  if (!send_command(bus, options.node_id, 0x0F, 10, 1, 0x05, 20)) {
    child.stop();
    out.detail = "send failed";
    return out;
  }
  const auto status =
      wait_status(bus, options.node_id, 1, std::chrono::milliseconds{1000});
  child.stop();
  if (!status || status->result != rcr::can_v1::OutputResult::Applied ||
      status->output_mirror != 0x05) {
    out.detail = "missing or unexpected OutputStatus";
    return out;
  }
  out.passed = true;
  out.detail = "applied mirror=0x05";
  return out;
}

ScenarioResult scenario_output_lease_neutralizes(rcr::SocketCan& bus,
                                                 ChildProcess& child,
                                                 const Options& options) {
  // 独立进程黑盒验证：首条命令只给 50ms lease；等待其到期后用 partial mask 探测旧高位。
  // 若模拟器仍无限保持 0xA5，第二条只清 bit0 后会返回 0xA4；正确归零则返回 0x00。
  ScenarioResult out{"output_lease_neutralizes", false, {}};
  std::vector<std::string> args{
      "--can", options.can_if, "--node-id", std::to_string(options.node_id),
      "--heartbeat-ms", "50", "--duration-ms", "8000", "--session-id", "12",
  };
  if (!child.start(options.sim_path, args, "/tmp/rcr_vcan_acc_lease.log")) {
    out.detail = "failed to start simulator";
    return out;
  }
  if (!wait_heartbeat(bus, options.node_id, std::chrono::milliseconds{1000})) {
    child.stop();
    out.detail = "no heartbeat before lease test";
    return out;
  }
  if (!send_command(bus, options.node_id, 0xFF, 12, 101, 0xA5, 5)) {
    child.stop();
    out.detail = "seed command send failed";
    return out;
  }
  const auto seed =
      wait_status(bus, options.node_id, 101, std::chrono::milliseconds{1000});
  if (!seed || seed->result != rcr::can_v1::OutputResult::Applied ||
      seed->output_mirror != 0xA5) {
    child.stop();
    out.detail = "seed output was not applied";
    return out;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds{150});
  if (!send_command(bus, options.node_id, 0x01, 12, 102, 0x00, 20)) {
    child.stop();
    out.detail = "probe command send failed";
    return out;
  }
  const auto probe =
      wait_status(bus, options.node_id, 102, std::chrono::milliseconds{1000});
  child.stop();
  if (!probe || probe->result != rcr::can_v1::OutputResult::Applied ||
      probe->output_mirror != 0x00) {
    out.detail = probe ? "stale output survived lease; mirror=" +
                             std::to_string(probe->output_mirror)
                       : "missing probe OutputStatus";
    return out;
  }
  out.passed = true;
  out.detail = "50ms lease expired before partial-mask probe";
  return out;
}

ScenarioResult scenario_reject_rules(rcr::SocketCan& bus, ChildProcess& child,
                                     const Options& options) {
  ScenarioResult out{"reject_stale_session_expired", false, {}};
  // 延迟应用 50ms + validity 10ms → EXPIRED。
  std::vector<std::string> args{
      "--can",
      options.can_if,
      "--node-id",
      std::to_string(options.node_id),
      "--heartbeat-ms",
      "50",
      "--duration-ms",
      "10000",
      "--session-id",
      "3",
      "--fault-delay-response-ms",
      "50",
  };
  if (!child.start(options.sim_path, args, "/tmp/rcr_vcan_acc_reject.log")) {
    out.detail = "failed to start simulator";
    return out;
  }
  if (!wait_heartbeat(bus, options.node_id, std::chrono::milliseconds{1000})) {
    child.stop();
    out.detail = "no heartbeat";
    return out;
  }

  // 先发一条立即会进 pending 的短有效期命令（seq=1）。
  if (!send_command(bus, options.node_id, 1, 3, 1, 1, 1)) {
    child.stop();
    out.detail = "send expired candidate failed";
    return out;
  }
  const auto expired =
      wait_status(bus, options.node_id, 1, std::chrono::milliseconds{1000});
  if (!expired || expired->result != rcr::can_v1::OutputResult::Expired) {
    child.stop();
    out.detail = "expected EXPIRED";
    return out;
  }

  // 重启子进程清除延迟注入与 sequence 历史，再单独测 stale/session，避免一个故障机制
  // 同时改变多个变量导致失败原因不可归因。
  child.stop();
  args = {"--can",         options.can_if,
          "--node-id",     std::to_string(options.node_id),
          "--heartbeat-ms", "50",
          "--duration-ms", "8000",
          "--session-id",  "3"};
  if (!child.start(options.sim_path, args, "/tmp/rcr_vcan_acc_reject2.log")) {
    out.detail = "failed to restart simulator";
    return out;
  }
  if (!wait_heartbeat(bus, options.node_id, std::chrono::milliseconds{1000})) {
    child.stop();
    out.detail = "no heartbeat (phase2)";
    return out;
  }
  if (!send_command(bus, options.node_id, 1, 3, 5, 1, 20) ||
      !wait_status(bus, options.node_id, 5, std::chrono::milliseconds{1000})) {
    child.stop();
    out.detail = "seed command failed";
    return out;
  }
  if (!send_command(bus, options.node_id, 1, 3, 5, 1, 20)) {
    child.stop();
    out.detail = "dup send failed";
    return out;
  }
  const auto stale =
      wait_status(bus, options.node_id, 5, std::chrono::milliseconds{1000});
  if (!stale || stale->result != rcr::can_v1::OutputResult::StaleSequence) {
    child.stop();
    out.detail = "expected STALE_SEQUENCE";
    return out;
  }
  if (!send_command(bus, options.node_id, 1, 3, 4, 1, 20)) {
    child.stop();
    out.detail = "older send failed";
    return out;
  }
  const auto older =
      wait_status(bus, options.node_id, 4, std::chrono::milliseconds{1000});
  if (!older || older->result != rcr::can_v1::OutputResult::StaleSequence) {
    child.stop();
    out.detail = "expected STALE for older seq";
    return out;
  }
  if (!send_command(bus, options.node_id, 1, 99, 6, 1, 20)) {
    child.stop();
    out.detail = "bad session send failed";
    return out;
  }
  const auto mismatch =
      wait_status(bus, options.node_id, 6, std::chrono::milliseconds{1000});
  child.stop();
  if (!mismatch ||
      mismatch->result != rcr::can_v1::OutputResult::SessionMismatch) {
    out.detail = "expected SESSION_MISMATCH";
    return out;
  }
  out.passed = true;
  out.detail = "expired+stale+session ok";
  return out;
}

ScenarioResult scenario_heartbeat_loss(rcr::SocketCan& bus, ChildProcess& child,
                                       const Options& options) {
  ScenarioResult out{"heartbeat_loss", false, {}};
  std::vector<std::string> args{
      "--can", options.can_if, "--node-id", std::to_string(options.node_id),
      "--heartbeat-ms", "50", "--duration-ms", "8000",
  };
  if (!child.start(options.sim_path, args, "/tmp/rcr_vcan_acc_loss.log")) {
    out.detail = "failed to start simulator";
    return out;
  }
  if (!wait_heartbeat(bus, options.node_id, std::chrono::milliseconds{1000})) {
    child.stop();
    out.detail = "never saw heartbeat";
    return out;
  }
  child.stop();  // 对端退出 = 可重复通信中断，不依赖人工拔线抢时机。

  const auto quiet_deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds{400};
  bool saw = false;
  while (std::chrono::steady_clock::now() < quiet_deadline) {
    if (recv_one(bus, std::chrono::milliseconds{50})) {
      // 可能仍有内核队列残留；只关心 Heartbeat。
    }
  }
  // 再观察 300ms，不应再出现新的合法 heartbeat。
  saw = wait_heartbeat(bus, options.node_id, std::chrono::milliseconds{300}).has_value();
  if (saw) {
    out.detail = "heartbeat continued after peer exit";
    return out;
  }
  out.passed = true;
  out.detail = "silence after peer stop (>=300ms)";
  return out;
}

ScenarioResult scenario_restart_session(rcr::SocketCan& bus, ChildProcess& child,
                                        const Options& options) {
  // soft restart 保持同一 OS 进程但更换 boot/session，专门验证协议会话边界；它不冒充
  // 断电重启证据，真正进程/设备重启要在后续部署和硬件阶段另测。
  ScenarioResult out{"restart_new_session", false, {}};
  std::vector<std::string> args{
      "--can",
      options.can_if,
      "--node-id",
      std::to_string(options.node_id),
      "--heartbeat-ms",
      "50",
      "--duration-ms",
      "10000",
      "--boot-id",
      "1",
      "--session-id",
      "1",
      "--fault-restart-after-ms",
      "250",
  };
  if (!child.start(options.sim_path, args, "/tmp/rcr_vcan_acc_restart.log")) {
    out.detail = "failed to start simulator";
    return out;
  }
  const auto hb1 =
      wait_heartbeat(bus, options.node_id, std::chrono::milliseconds{1000});
  if (!hb1 || hb1->session_id != 1) {
    child.stop();
    out.detail = "missing initial session";
    return out;
  }
  std::optional<rcr::can_v1::WireHeartbeat> hb2;
  for (int i = 0; i < 40; ++i) {
    hb2 = wait_heartbeat(bus, options.node_id, std::chrono::milliseconds{100});
    if (hb2 && hb2->session_id != 1) {
      break;
    }
  }
  if (!hb2 || hb2->session_id == 1) {
    child.stop();
    out.detail = "session did not change after restart";
    return out;
  }
  if (!send_command(bus, options.node_id, 1, 1, 1, 1, 20)) {
    child.stop();
    out.detail = "old session send failed";
    return out;
  }
  const auto old =
      wait_status(bus, options.node_id, 1, std::chrono::milliseconds{1000});
  if (!old || old->result != rcr::can_v1::OutputResult::SessionMismatch) {
    child.stop();
    out.detail = "old session not rejected";
    return out;
  }
  if (!send_command(bus, options.node_id, 1, hb2->session_id, 1, 1, 20)) {
    child.stop();
    out.detail = "new session send failed";
    return out;
  }
  const auto neu =
      wait_status(bus, options.node_id, 1, std::chrono::milliseconds{1000});
  child.stop();
  if (!neu || neu->result != rcr::can_v1::OutputResult::Applied) {
    out.detail = "new session command not applied";
    return out;
  }
  out.passed = true;
  out.detail = "old rejected, new session=" + std::to_string(hb2->session_id);
  return out;
}

ScenarioResult scenario_illegal_frames(rcr::SocketCan& bus, ChildProcess& child,
                                       const Options& options) {
  ScenarioResult out{"illegal_frames", false, {}};
  std::vector<std::string> args{
      "--can", options.can_if, "--node-id", std::to_string(options.node_id),
      "--heartbeat-ms", "50", "--duration-ms", "8000", "--session-id", "1",
  };
  if (!child.start(options.sim_path, args, "/tmp/rcr_vcan_acc_illegal.log")) {
    out.detail = "failed to start simulator";
    return out;
  }
  if (!wait_heartbeat(bus, options.node_id, std::chrono::milliseconds{1000})) {
    child.stop();
    out.detail = "no heartbeat";
    return out;
  }

  int local_rejects = 0;
  // 错误 DLC
  rcr::CanFrame dlc7{};
  dlc7.can_id =
      rcr::can_v1::make_can_id(rcr::can_v1::Function::OutputCommand, options.node_id);
  dlc7.len = 7;
  dlc7.data[0] = 1;
  dlc7.data[1] = 1;
  (void)bus.send(dlc7);
  if (!rcr::can_v1::decode_output_command(dlc7)) {
    ++local_rejects;
  }

  // 未知版本
  rcr::CanFrame bad_ver = dlc7;
  bad_ver.len = 8;
  bad_ver.data[0] = 2;
  bad_ver.data[1] = 1;
  bad_ver.data[3] = 1;
  bad_ver.data[5] = 1;
  bad_ver.data[7] = 10;
  (void)bus.send(bad_ver);
  if (!rcr::can_v1::decode_output_command(bad_ver)) {
    ++local_rejects;
  }

  // 非法 flags（对 heartbeat 布局的保留位；这里用零 mask 命令）
  rcr::CanFrame zero_mask = bad_ver;
  zero_mask.data[0] = 1;
  zero_mask.data[1] = 0;
  (void)bus.send(zero_mask);
  if (!rcr::can_v1::decode_output_command(zero_mask)) {
    ++local_rejects;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds{100});
  if (!send_command(bus, options.node_id, 1, 1, 1, 1, 20)) {
    child.stop();
    out.detail = "recovery command send failed";
    return out;
  }
  const auto ok =
      wait_status(bus, options.node_id, 1, std::chrono::milliseconds{1000});
  child.stop();

  std::ifstream log(child.log_path());
  std::string log_text((std::istreambuf_iterator<char>(log)),
                       std::istreambuf_iterator<char>());
  std::uint64_t sim_rejects = 0;
  const auto marker = std::string("protocol_rejects=");
  const auto pos = log_text.find(marker);
  if (pos != std::string::npos) {
    sim_rejects = static_cast<std::uint64_t>(
        std::strtoull(log_text.c_str() + pos + marker.size(), nullptr, 10));
  }
  const bool counted = sim_rejects > 0;

  if (!ok || ok->result != rcr::can_v1::OutputResult::Applied) {
    out.detail = "node did not recover after illegal frames";
    return out;
  }
  if (local_rejects < 3) {
    out.detail = "local decode reject count too low";
    return out;
  }
  if (!counted) {
    out.detail = "simulator log missing non-zero protocol_rejects";
    return out;
  }
  out.passed = true;
  out.detail = "local_rejects=" + std::to_string(local_rejects) +
               " sim_rejects=" + std::to_string(sim_rejects);
  return out;
}

std::string collect_metadata(const Options& options) {
  utsname uts{};
  ::uname(&uts);
  std::ostringstream oss;
  oss << "kernel=" << uts.release << "\n"
      << "machine=" << uts.machine << "\n"
      << "compiler=" << __VERSION__ << "\n"
      << "can_if=" << options.can_if << "\n"
      << "node_id=" << static_cast<int>(options.node_id) << "\n"
      << "sim_path=" << options.sim_path << "\n";

  std::string commit = "unknown";
  FILE* pipe = ::popen("git rev-parse --short HEAD 2>/dev/null", "r");
  if (pipe != nullptr) {
    char buf[64]{};
    if (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
      commit = buf;
      while (!commit.empty() &&
             (commit.back() == '\n' || commit.back() == '\r')) {
        commit.pop_back();
      }
      if (commit.empty()) {
        commit = "unknown";
      }
    }
    ::pclose(pipe);
  }
  oss << "git=" << commit << "\n";

  // commit id 只能标识 HEAD，不能代表当前实际参与测试的源码内容。
  // 若工作树存在已修改或未跟踪文件，必须明确写入证据，否则同一个 commit
  // 可能对应多份不同的可执行文件，验收结果将无法复现。
  std::string git_dirty = "unknown";
  pipe = ::popen(
      "git status --porcelain --untracked-files=normal 2>/dev/null", "r");
  if (pipe != nullptr) {
    const int first = std::fgetc(pipe);
    const int status = ::pclose(pipe);
    if (status == 0) {
      git_dirty = first == EOF ? "false" : "true";
    }
  }
  oss << "git_dirty=" << git_dirty << "\n";
  oss << "note=vcan software path only; not physical CAN evidence\n";
  return oss.str();
}

}  // namespace

int main(int argc, char** argv) {
  Options options{};
  if (!parse_options(argc, argv, options)) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  // 阶段验收：接口缺失必须失败。
  if (!rcr::can_interface_available(options.can_if)) {
    std::cerr << "FAIL: CAN interface '" << options.can_if
              << "' unavailable; run sudo ./linux/scripts/setup_vcan.sh "
              << options.can_if << "\n";
    return EXIT_FAILURE;
  }
  if (::access(options.sim_path.c_str(), X_OK) != 0) {
    std::cerr << "FAIL: simulator not executable: " << options.sim_path << "\n";
    return EXIT_FAILURE;
  }

  rcr::SocketCan bus(options.can_if);
  if (auto rc = bus.open(); !rc) {
    std::cerr << "FAIL: open " << options.can_if << ": " << rc.error().message()
              << "\n";
    return EXIT_FAILURE;
  }

  const std::string meta = collect_metadata(options);
  std::cout << "=== rcr_vcan_acceptance metadata ===\n" << meta;

  ChildProcess child;
  std::vector<ScenarioResult> results;
  results.push_back(scenario_heartbeat_status(bus, child, options));
  results.push_back(scenario_command_loop(bus, child, options));
  results.push_back(scenario_output_lease_neutralizes(bus, child, options));
  results.push_back(scenario_reject_rules(bus, child, options));
  results.push_back(scenario_heartbeat_loss(bus, child, options));
  results.push_back(scenario_restart_session(bus, child, options));
  results.push_back(scenario_illegal_frames(bus, child, options));

  int failed = 0;
  std::cout << "=== scenarios ===\n";
  for (const auto& result : results) {
    std::cout << (result.passed ? "PASS" : "FAIL") << "  " << result.name << "  "
              << result.detail << "\n";
    if (!result.passed) {
      ++failed;
    }
  }

  if (!options.evidence_path.empty()) {
    std::ofstream out(options.evidence_path);
    out << meta;
    for (const auto& result : results) {
      out << (result.passed ? "PASS" : "FAIL") << "\t" << result.name << "\t"
          << result.detail << "\n";
    }
  }

  bus.close();
  child.stop();
  std::cout << "=== summary: " << (results.size() - static_cast<std::size_t>(failed))
            << " passed, " << failed << " failed ===\n";
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
