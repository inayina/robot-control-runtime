// 手工演示客户端：连上参考从站后顺序发三笔（outstanding=1）。
// 对照抓包 / 笔记：docs/MODBUS_TCP_NOTES.md「抓包对照」。
// 默认 unit_id=0 与 demo server 日志中的 unit 一致；可用 --unit 覆盖。

#include "rcr_mbus/client.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
  rcr::mbus::ClientConfig cfg;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
      cfg.host = argv[++i];
    } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      cfg.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
    } else if (std::strcmp(argv[i], "--unit") == 0 && i + 1 < argc) {
      cfg.unit_id = static_cast<std::uint8_t>(std::atoi(argv[++i]));
    }
  }

  rcr::mbus::Client client(cfg);
  auto conn = client.connect();
  if (!conn) {
    std::cerr << "connect failed: " << conn.message << "\n";
    return 1;
  }

  // ① FC 0x06：写 holding[10]=0xBEEF
  auto wr = client.write_single(10, 0xBEEF);
  if (!wr) {
    std::cerr << "write_single failed: " << wr.message << "\n";
    return 1;
  }
  // ② FC 0x03：读 holding[0..3]（期望看到 server seed 的 0x1234 / 0xABCD）
  auto rd = client.read_holding(0, 4);
  if (!rd) {
    std::cerr << "read_holding failed: " << rd.message << "\n";
    return 1;
  }
  std::cout << "read holding[0..3]:";
  for (auto v : rd.value) {
    std::cout << " 0x" << std::hex << v;
  }
  std::cout << std::dec << "\n";

  // ③ FC 0x10：从 addr 20 起写三个寄存器
  const std::vector<std::uint16_t> multi{1, 2, 3};
  auto wm = client.write_multiple(20, multi);
  if (!wm) {
    std::cerr << "write_multiple failed: " << wm.message << "\n";
    return 1;
  }
  std::cout << "write_multiple ok addr=" << wm.value.address << " qty=" << wm.value.quantity
            << "\n";
  return 0;
}
