// 参考 Holding 从站：监听学习端口 1502，预置两个寄存器方便手动/抓包演示。
// Unit ID 不参与 map 寻址（本教学 server 忽略 Unit，仍回显请求中的 Unit）。
// 与 Runtime / 1 ms 闭环无关；Ctrl-C 或杀进程结束。

#include "rcr_mbus/server.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

void usage(const char* argv0) {
  std::cerr << "usage: " << argv0 << " [--host 127.0.0.1] [--port 1502] [--regs 64]\n";
}

}  // namespace

int main(int argc, char** argv) {
  rcr::mbus::ServerConfig cfg;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
      cfg.bind_host = argv[++i];
    } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      cfg.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
    } else if (std::strcmp(argv[i], "--regs") == 0 && i + 1 < argc) {
      cfg.holding_count = static_cast<std::size_t>(std::atoi(argv[++i]));
    } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      return 0;
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  // 预置几个寄存器便于手动演示与抓包对照。
  rcr::mbus::RefServer server(cfg);
  auto started = server.start();
  if (!started) {
    std::cerr << "start failed: " << started.message << "\n";
    return 1;
  }
  (void)server.map().write_single(0, 0x1234);
  (void)server.map().write_single(1, 0xABCD);

  std::cout << "mbus_ref_server listening on " << cfg.bind_host << ":" << server.port()
            << " unit_id ignored-for-map holding=" << cfg.holding_count << std::endl;
  std::cout << "seed holding[0]=0x1234 holding[1]=0xABCD" << std::endl;
  std::cout << "Ctrl-C to stop (or kill)." << std::endl;

  // 前台挂起直到信号/kill；服务循环在 server 内部线程。
  while (server.running()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  return 0;
}
