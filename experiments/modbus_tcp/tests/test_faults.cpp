#include "check.hpp"
#include "rcr_mbus/client.hpp"
#include "rcr_mbus/codec.hpp"
#include "rcr_mbus/server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace rcr::mbus;

static void test_response_timeout() {
  ServerConfig scfg;
  scfg.port = 0;
  scfg.response_delay = std::chrono::milliseconds(400);
  RefServer server(scfg);
  CHECK(server.start());

  ClientConfig ccfg;
  ccfg.port = server.port();
  ccfg.response_timeout = std::chrono::milliseconds(50);
  ccfg.reconnect_attempts = 1;
  Client client(ccfg);
  CHECK(client.connect());
  auto r = client.read_holding(0, 1);
  CHECK(r.error == Error::Timeout);
  server.stop();
}

static void test_connect_timeout() {
  // 未监听的高端口：应 connect timeout 或立即 refused。
  ClientConfig ccfg;
  ccfg.host = "127.0.0.1";
  ccfg.port = 1;  // 通常拒绝或不可用
  ccfg.connect_timeout = std::chrono::milliseconds(200);
  ccfg.reconnect_attempts = 1;
  Client client(ccfg);
  auto r = client.connect();
  CHECK(!r);
  CHECK(r.error == Error::Io || r.error == Error::Timeout);
}

static void test_transaction_mismatch_detection() {
  // 手工建一个假服务：回错误 TID。
  const int lfd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  CHECK(lfd >= 0);
  int yes = 1;
  ::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  CHECK(::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
  CHECK(::listen(lfd, 1) == 0);
  socklen_t alen = sizeof(addr);
  CHECK(::getsockname(lfd, reinterpret_cast<sockaddr*>(&addr), &alen) == 0);
  const std::uint16_t port = ntohs(addr.sin_port);

  std::thread th([&] {
    const int c = ::accept4(lfd, nullptr, nullptr, SOCK_CLOEXEC);
    if (c < 0) {
      return;
    }
    std::uint8_t buf[64];
    const ssize_t n = ::recv(c, buf, sizeof(buf), 0);
    if (n >= 7) {
      // 篡改 TID
      buf[0] = 0xEE;
      buf[1] = 0xEE;
      // 简化：回一个合法 read response 外壳
      std::uint8_t resp[] = {0xEE, 0xEE, 0x00, 0x00, 0x00, 0x05, 0x01, 0x03, 0x02, 0x00, 0x00};
      (void)::send(c, resp, sizeof(resp), MSG_NOSIGNAL);
    }
    ::close(c);
  });

  ClientConfig ccfg;
  ccfg.port = port;
  ccfg.reconnect_attempts = 1;
  Client client(ccfg);
  CHECK(client.connect());
  auto r = client.read_holding(0, 1);
  CHECK(r.error == Error::TransactionMismatch);
  th.join();
  ::close(lfd);
}

static void test_reconnect_after_close() {
  ServerConfig scfg;
  scfg.port = 0;
  RefServer server(scfg);
  CHECK(server.start());

  ClientConfig ccfg;
  ccfg.port = server.port();
  ccfg.reconnect_base = std::chrono::milliseconds(10);
  ccfg.reconnect_max = std::chrono::milliseconds(50);
  ccfg.reconnect_attempts = 10;
  Client client(ccfg);
  CHECK(client.connect());
  CHECK(client.write_single(0, 1));

  // 停掉 server 触发失败，再重启，客户端应能重连并成功。
  const auto port = server.port();
  server.stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  ServerConfig scfg2;
  scfg2.port = port;
  RefServer server2(scfg2);
  // 端口可能短暂占用；重试绑定
  bool up = false;
  for (int i = 0; i < 20; ++i) {
    auto st = server2.start();
    if (st) {
      up = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(up);

  auto r = client.write_single(0, 2);
  CHECK_MSG(r, r.message);
  server2.stop();
}

int main() {
  test_connect_timeout();
  test_response_timeout();
  test_transaction_mismatch_detection();
  test_reconnect_after_close();
  return 0;
}
