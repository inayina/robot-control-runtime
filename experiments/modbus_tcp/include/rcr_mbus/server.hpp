#pragma once

// 参考从站：单线程 accept + 顺序处理连接。
// 每连接语义：读完一完整 ADU → handle_pdu → 回一帧（自然 outstanding≈1）。
// 不做线程池：教学清晰；多客户端并发不是本阶段退出条件。
// response_delay 仅供测客户端 timeout，生产路径应保持 0。

#include "rcr_mbus/framing.hpp"
#include "rcr_mbus/register_map.hpp"
#include "rcr_mbus/types.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace rcr::mbus {

struct ServerConfig {
  std::string bind_host = "127.0.0.1";
  std::uint16_t port = 1502;
  std::size_t holding_count = 64;
  // 测试注入：收到请求后额外延迟，用于测 response timeout。
  std::chrono::milliseconds response_delay{0};
};

class RefServer {
 public:
  explicit RefServer(ServerConfig cfg = {});
  ~RefServer();

  RefServer(const RefServer&) = delete;
  RefServer& operator=(const RefServer&) = delete;

  Result<bool> start();
  void stop();
  bool running() const { return running_.load(); }
  std::uint16_t port() const { return bound_port_; }
  HoldingMap& map() { return map_; }

 private:
  void thread_main();
  void serve_client(int client_fd);

  ServerConfig cfg_;
  HoldingMap map_;
  int listen_fd_ = -1;
  std::uint16_t bound_port_ = 0;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace rcr::mbus
