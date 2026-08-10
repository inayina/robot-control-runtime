// 进程级 rcrd 验收：独立 rcrd + rcr_node_sim，只经 vcan0；验证退出码与 SIGTERM。
#include "rcr/can_bus.hpp"
#include "rcr/vcan.hpp"
#include "test_support.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef RCR_RCRD_PATH
#define RCR_RCRD_PATH "./rcrd"
#endif
#ifndef RCR_NODE_SIM_PATH
#define RCR_NODE_SIM_PATH "./rcr_node_sim"
#endif

namespace {

class ChildProcess {
 public:
  ~ChildProcess() { stop(); }

  bool start(const std::string& path, const std::vector<std::string>& args,
             const std::string& log_path) {
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
      std::_Exit(127);
    }
    ::close(log_fd);
    pid_ = pid;
    return true;
  }

  int wait_for_exit(std::chrono::milliseconds budget) {
    if (pid_ <= 0) {
      return -1;
    }
    const auto deadline = std::chrono::steady_clock::now() + budget;
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
      const pid_t rc = ::waitpid(pid_, &status, WNOHANG);
      if (rc == pid_) {
        pid_ = -1;
        if (WIFEXITED(status)) {
          return WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status)) {
          return 128 + WTERMSIG(status);
        }
        return -1;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    return -1;
  }

  void stop() {
    if (pid_ <= 0) {
      return;
    }
    ::kill(pid_, SIGTERM);
    int status = 0;
    for (int i = 0; i < 50; ++i) {
      if (::waitpid(pid_, &status, WNOHANG) == pid_) {
        pid_ = -1;
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    ::kill(pid_, SIGKILL);
    ::waitpid(pid_, &status, 0);
    pid_ = -1;
  }

  [[nodiscard]] pid_t pid() const noexcept { return pid_; }

 private:
  pid_t pid_{-1};
  std::string log_path_{};
};

void require_vcan_or_skip() {
  if (rcr::probe_can_interface("vcan0") != rcr::CanInterfaceStatus::Available) {
    RCR_SKIP("vcan0 missing");
  }
  rcr::SocketCan probe_bus{"vcan0"};
  auto opened = probe_bus.open();
  if (!opened) {
    RCR_SKIP(std::string("cannot open vcan0: ") + opened.error().message());
  }
  probe_bus.close();
}

bool log_contains(const std::string& path, const std::string& needle) {
  std::ifstream input(path);
  const std::string text((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
  return text.find(needle) != std::string::npos;
}

}  // namespace

RCR_TEST(RcrdHelpExitsZero) {
  ChildProcess proc;
  RCR_REQUIRE(proc.start(RCR_RCRD_PATH, {"--help"}, "/tmp/rcrd_help.log"));
  const int code = proc.wait_for_exit(std::chrono::milliseconds{2000});
  RCR_EXPECT(code == 0);
}

RCR_TEST(RcrdMissingIfaceExitsTwo) {
  ChildProcess proc;
  RCR_REQUIRE(proc.start(RCR_RCRD_PATH,
                         {"--can", "no_such_iface_rcrd", "--duration-ms", "100"},
                         "/tmp/rcrd_missing.log"));
  const int code = proc.wait_for_exit(std::chrono::milliseconds{2000});
  RCR_EXPECT(code == 2);
}

RCR_TEST(RcrdSigtermBoundedExit) {
  require_vcan_or_skip();

  ChildProcess sim;
  RCR_REQUIRE(sim.start(RCR_NODE_SIM_PATH,
                        {"--can", "vcan0", "--node-id", "1", "--heartbeat-ms", "50"},
                        "/tmp/rcrd_sim.log"));

  ChildProcess daemon;
  RCR_REQUIRE(daemon.start(RCR_RCRD_PATH,
                           {"--can", "vcan0", "--node-id", "1", "--period-ms", "10"},
                           "/tmp/rcrd_run.log"));
  std::this_thread::sleep_for(std::chrono::milliseconds{300});
  RCR_REQUIRE(daemon.pid() > 0);
  ::kill(daemon.pid(), SIGTERM);
  const int code = daemon.wait_for_exit(std::chrono::milliseconds{3000});
  RCR_EXPECT(code == 0);
  sim.stop();
}

RCR_TEST(RcrdDurationExit) {
  require_vcan_or_skip();
  ChildProcess daemon;
  RCR_REQUIRE(daemon.start(RCR_RCRD_PATH,
                           {"--can", "vcan0", "--duration-ms", "200"},
                           "/tmp/rcrd_duration.log"));
  const int code = daemon.wait_for_exit(std::chrono::milliseconds{3000});
  RCR_EXPECT(code == 0);
  RCR_EXPECT(log_contains("/tmp/rcrd_duration.log", "msg=final summary"));
  RCR_EXPECT(log_contains("/tmp/rcrd_duration.log", "queue_drop_count="));
  RCR_EXPECT(log_contains("/tmp/rcrd_duration.log", "ack_timeout_count="));
}

RCR_TEST(RcrdRepeatStartStopFdStable) {
  require_vcan_or_skip();

  // 进程级：每次 fork/exec 新 rcrd，采样运行中子进程 fd 数应稳定；父进程 fd/线程
  // 也不应随循环增长。仅 wait 退出码不能证明“无 fd 泄漏”——旧测试曾空调用 count_fds。
  const pid_t self = ::getpid();
  const int parent_fds_before = rcr::test::count_proc_fds(self);
  const int parent_threads_before = rcr::test::count_proc_threads(self);
  RCR_REQUIRE(parent_fds_before > 0);
  RCR_REQUIRE(parent_threads_before > 0);

  int child_fds_baseline = -1;
  constexpr int kIterations = 50;
  for (int i = 0; i < kIterations; ++i) {
    ChildProcess daemon;
    RCR_REQUIRE(daemon.start(RCR_RCRD_PATH,
                             {"--can", "vcan0", "--duration-ms", "200"},
                             "/tmp/rcrd_repeat.log"));
    RCR_REQUIRE(daemon.pid() > 0);

    // “fd >= 5”只是启动中间态，不代表 SocketCAN/epoll/eventfd/signalfd 都已就绪。等待
    // 生产代码已有的 started 日志作为握手，再读取 /proc，避免用任意 sleep 猜启动完成。
    bool ready = false;
    for (int attempt = 0; attempt < 80; ++attempt) {
      if (log_contains("/tmp/rcrd_repeat.log", "msg=daemon started")) {
        ready = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    RCR_REQUIRE(ready);
    const int child_fds = rcr::test::count_proc_fds(daemon.pid());
    RCR_REQUIRE(child_fds >= 5);

    if (child_fds_baseline < 0) {
      child_fds_baseline = child_fds;
    } else {
      // 允许 ±1：采样瞬间可能撞上短寿命内部 fd；持续上涨才是泄漏/回归。
      const int delta = child_fds - child_fds_baseline;
      RCR_EXPECT(delta >= -1 && delta <= 1);
    }

    const int code = daemon.wait_for_exit(std::chrono::milliseconds{3000});
    RCR_REQUIRE(code == 0);

    RCR_EXPECT(rcr::test::count_proc_threads(self) == parent_threads_before);
    RCR_EXPECT(rcr::test::count_proc_fds(self) == parent_fds_before);
  }
  RCR_EXPECT(child_fds_baseline >= 5);
}

RCR_TEST_MAIN()
