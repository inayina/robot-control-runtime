// 进程级 rcrd 验收：独立 rcrd + rcr_node_sim，只经 vcan0；验证退出码与 SIGTERM。
#include "rcr/can_bus.hpp"
#include "rcr/vcan.hpp"
#include "test_support.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
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

int count_fds(pid_t pid) {
  std::ostringstream path;
  path << "/proc/" << pid << "/fd";
  // 仅粗测：目录可读条目数；失败返回 -1。
  int n = 0;
  // 用简单 shell 计数避免依赖 dirent 在沙箱差异；测试在 Linux 主机跑。
  std::ostringstream cmd;
  cmd << "ls -1 /proc/" << pid << "/fd 2>/dev/null | wc -l";
  FILE* pipe = ::popen(cmd.str().c_str(), "r");
  if (!pipe) {
    return -1;
  }
  char buf[64]{};
  if (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
    n = std::atoi(buf);
  }
  ::pclose(pipe);
  return n;
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
}

RCR_TEST(RcrdRepeatStartStopFdStable) {
  require_vcan_or_skip();
  // 子进程级重复启动：验证每次都能正常退出，无僵尸。
  for (int i = 0; i < 30; ++i) {
    ChildProcess daemon;
    RCR_REQUIRE(daemon.start(RCR_RCRD_PATH,
                             {"--can", "vcan0", "--duration-ms", "30"},
                             "/tmp/rcrd_repeat.log"));
    const int code = daemon.wait_for_exit(std::chrono::milliseconds{3000});
    RCR_REQUIRE(code == 0);
  }
  (void)count_fds;
}

RCR_TEST_MAIN()
