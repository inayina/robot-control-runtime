#include "check.hpp"
#include "rcr_mbus/client.hpp"
#include "rcr_mbus/server.hpp"

#include <chrono>
#include <thread>
#include <vector>

using namespace rcr::mbus;

int main() {
  ServerConfig scfg;
  scfg.bind_host = "127.0.0.1";
  scfg.port = 0;  // ephemeral
  scfg.holding_count = 64;
  RefServer server(scfg);
  auto st = server.start();
  CHECK_MSG(st, st.message);
  CHECK(server.port() != 0);

  ClientConfig ccfg;
  ccfg.host = "127.0.0.1";
  ccfg.port = server.port();
  ccfg.unit_id = 1;
  ccfg.connect_timeout = std::chrono::milliseconds(500);
  ccfg.response_timeout = std::chrono::milliseconds(500);
  Client client(ccfg);
  auto conn = client.connect();
  CHECK_MSG(conn, conn.message);

  auto w = client.write_single(3, 0xCAFE);
  CHECK_MSG(w, w.message);
  CHECK(w.value.address == 3);
  CHECK(w.value.value == 0xCAFE);

  const std::vector<std::uint16_t> vals{0x10, 0x20, 0x30};
  auto wm = client.write_multiple(10, vals);
  CHECK_MSG(wm, wm.message);
  CHECK(wm.value.quantity == 3);

  auto rd = client.read_holding(3, 1);
  CHECK_MSG(rd, rd.message);
  CHECK(rd.value.size() == 1);
  CHECK(rd.value[0] == 0xCAFE);

  auto rd2 = client.read_holding(10, 3);
  CHECK_MSG(rd2, rd2.message);
  CHECK(rd2.value == vals);

  // illegal address → exception
  auto bad = client.read_holding(1000, 1);
  CHECK(bad.error == Error::ExceptionResponse);

  server.stop();
  return 0;
}
