#include "check.hpp"
#include "rcr_mbus/client.hpp"
#include "rcr_mbus/server.hpp"

#include <modbus/modbus.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace rcr::mbus;

// 我们的 client → libmodbus TCP server
static void test_our_client_vs_libmodbus_server() {
  modbus_t* ctx = modbus_new_tcp("127.0.0.1", 0);
  CHECK(ctx != nullptr);
  modbus_set_slave(ctx, 1);

  const int server_socket = modbus_tcp_listen(ctx, 1);
  CHECK(server_socket >= 0);

  // 查出实际监听端口（libmodbus 在 new_tcp 时若 port=0，需从 fd 取）
  sockaddr_in addr{};
  socklen_t alen = sizeof(addr);
  CHECK(::getsockname(server_socket, reinterpret_cast<sockaddr*>(&addr), &alen) == 0);
  const std::uint16_t port = ntohs(addr.sin_port);

  modbus_mapping_t* map =
      modbus_mapping_new(0, 0, 64, 0);  // holding registers only
  CHECK(map != nullptr);
  map->tab_registers[0] = 0x1111;
  map->tab_registers[1] = 0x2222;

  std::atomic<bool> done{false};
  std::thread srv([&] {
    int s = server_socket;
    if (modbus_tcp_accept(ctx, &s) < 0) {
      done.store(true);
      return;
    }
    uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
    while (!done.load()) {
      const int rc = modbus_receive(ctx, query);
      if (rc > 0) {
        (void)modbus_reply(ctx, query, rc, map);
      } else if (rc == -1) {
        break;
      }
    }
    // ctx 仍被 client 侧配置引用；关闭 accept 后的连接由 libmodbus 管理
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientConfig ccfg;
  ccfg.host = "127.0.0.1";
  ccfg.port = port;
  ccfg.unit_id = 1;
  Client client(ccfg);
  CHECK(client.connect());

  auto rd = client.read_holding(0, 2);
  CHECK_MSG(rd, rd.message);
  CHECK(rd.value.size() == 2);
  CHECK(rd.value[0] == 0x1111);
  CHECK(rd.value[1] == 0x2222);

  auto wr = client.write_single(5, 0xABCD);
  CHECK_MSG(wr, wr.message);
  CHECK(map->tab_registers[5] == 0xABCD);

  const std::vector<std::uint16_t> multi{7, 8, 9};
  auto wm = client.write_multiple(10, multi);
  CHECK_MSG(wm, wm.message);
  CHECK(map->tab_registers[10] == 7);
  CHECK(map->tab_registers[11] == 8);
  CHECK(map->tab_registers[12] == 9);

  done.store(true);
  client.close();
  // 打断 accept/receive
  ::shutdown(server_socket, SHUT_RDWR);
  ::close(server_socket);
  srv.join();
  modbus_mapping_free(map);
  modbus_free(ctx);
}

// libmodbus client → 我们的 reference server
static void test_libmodbus_client_vs_our_server() {
  ServerConfig scfg;
  scfg.port = 0;
  scfg.holding_count = 64;
  RefServer server(scfg);
  CHECK(server.start());
  (void)server.map().write_single(0, 0x42);

  modbus_t* ctx = modbus_new_tcp("127.0.0.1", server.port());
  CHECK(ctx != nullptr);
  modbus_set_slave(ctx, 1);
  CHECK(modbus_connect(ctx) == 0);

  uint16_t dest[4] = {};
  CHECK(modbus_read_registers(ctx, 0, 1, dest) == 1);
  CHECK(dest[0] == 0x42);

  CHECK(modbus_write_register(ctx, 2, 0x99) == 1);
  auto rd = server.map().read(2, 1);
  CHECK(rd);
  CHECK(rd.value[0] == 0x99);

  uint16_t src[3] = {1, 2, 3};
  CHECK(modbus_write_registers(ctx, 20, 3, src) == 3);
  auto rd2 = server.map().read(20, 3);
  CHECK(rd2);
  CHECK(rd2.value[0] == 1 && rd2.value[1] == 2 && rd2.value[2] == 3);

  modbus_close(ctx);
  modbus_free(ctx);
  server.stop();
}

int main() {
  test_our_client_vs_libmodbus_server();
  test_libmodbus_client_vs_our_server();
  return 0;
}
