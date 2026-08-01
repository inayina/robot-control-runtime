// Linux 目标测试；可选 vcan 集成，不依赖 MCU 工具链或硬件烧录环境。
#include "rcr/can_bus.hpp"
#include "rcr/vcan.hpp"
#include "test_support.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace {

bool require_vcan_from_args(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--require-vcan") == 0) {
      return true;
    }
  }
  const char* env = std::getenv("RCR_REQUIRE_VCAN");
  return env != nullptr && env[0] != '\0' && std::strcmp(env, "0") != 0;
}

void require_or_skip_vcan(bool require) {
  const auto status = rcr::probe_can_interface("vcan0");
  if (status == rcr::CanInterfaceStatus::Available) {
    return;
  }
  const char* detail = "unknown";
  switch (status) {
    case rcr::CanInterfaceStatus::Missing:
      detail = "missing";
      break;
    case rcr::CanInterfaceStatus::NotCan:
      detail = "not a CAN interface";
      break;
    case rcr::CanInterfaceStatus::InvalidName:
      detail = "invalid name";
      break;
    case rcr::CanInterfaceStatus::Available:
      break;
  }
  const std::string message =
      std::string("vcan0 unavailable (") + detail +
      "); run linux/scripts/setup_vcan.sh";
  if (require) {
    throw rcr::test::Failure(message);
  }
  RCR_SKIP(message);
}

bool g_require_vcan = false;

}  // namespace

/**
 * Linux vcan0 的可选集成测试。
 * 默认在接口缺失时 SKIP（CTest SKIP_RETURN_CODE=77）；
 * `--require-vcan` 或 RCR_REQUIRE_VCAN=1 时缺失即失败，供阶段验收使用。
 * 此测试只验证软件回环，不代表真实 CAN 物理层验收通过。
 */
RCR_TEST(SocketCanOnVcan0Loopback) {
  require_or_skip_vcan(g_require_vcan);

  rcr::SocketCan tx("vcan0");
  rcr::SocketCan rx("vcan0");
  RCR_EXPECT(tx.native_handle() == -1);
  RCR_REQUIRE(tx.open().ok());
  RCR_REQUIRE(rx.open().ok());
  RCR_EXPECT(tx.native_handle() >= 0);
  RCR_EXPECT(rx.native_handle() >= 0);
  RCR_EXPECT(tx.native_handle() != rx.native_handle());

  const int tx_fd = tx.native_handle();
  rcr::SocketCan moved(std::move(tx));
  RCR_EXPECT(moved.native_handle() == tx_fd);
  RCR_EXPECT(tx.native_handle() == -1);  // NOLINT(bugprone-use-after-move)

  rcr::CanFrame frame{};
  frame.can_id = 0x321;
  frame.len = 3;
  frame.data[0] = 0x01;
  frame.data[1] = 0x02;
  frame.data[2] = 0x03;

  // 发送后使用有限超时轮询，避免测试在接口异常时永久阻塞。
  RCR_REQUIRE(moved.send(frame).ok());

  bool got = false;
  for (int i = 0; i < 20; ++i) {
    auto result = rx.receive(std::chrono::milliseconds{50});
    if (result.ok()) {
      RCR_EXPECT(result.value().can_id == 0x321);
      RCR_EXPECT(result.value().len == 3);
      RCR_EXPECT(result.value().data[0] == 0x01);
      got = true;
      break;
    }
    if (result.error().code() != rcr::Errc::Timeout) {
      RCR_EXPECT(false && "unexpected receive error");
      break;
    }
  }
  RCR_EXPECT(got);

  moved.close();
  RCR_EXPECT(moved.native_handle() == -1);
  RCR_EXPECT(!moved.is_open());
}

int main(int argc, char** argv) {
  g_require_vcan = require_vcan_from_args(argc, argv);
  if (g_require_vcan) {
    std::cout << "mode: require vcan0 (stage acceptance)\n";
  } else {
    std::cout << "mode: optional vcan0 (skip if missing)\n";
  }
  return ::rcr::test::run_all();
}
