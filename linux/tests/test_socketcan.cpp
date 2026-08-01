// Linux 目标测试；不依赖 MCU 工具链或硬件烧录环境。
// 本文件只覆盖不依赖 vcan 的 SocketCAN 错误路径与 native_handle 生命周期。
#include "rcr/can_bus.hpp"
#include "test_support.hpp"

#include <utility>

RCR_TEST(SocketCanOpenMissingInterfaceFails) {
  rcr::SocketCan bus("vcan_rcr_does_not_exist_9f3a");
  RCR_EXPECT(bus.native_handle() < 0);
  auto rc = bus.open();
  RCR_EXPECT(!rc.ok());
  RCR_EXPECT(rc.error().code() == rcr::Errc::IoError);
  RCR_EXPECT(!bus.is_open());
  RCR_EXPECT(bus.native_handle() < 0);
}

RCR_TEST(SocketCanNativeHandleBeforeOpenIsInvalid) {
  rcr::SocketCan bus("vcan0");
  RCR_EXPECT(!bus.is_open());
  RCR_EXPECT(bus.native_handle() == -1);
}

RCR_TEST(SocketCanMoveTransfersNativeHandleOwnership) {
  // 不要求接口存在：用已打开失败后的空对象验证移动语义仍保持 -1。
  rcr::SocketCan original("vcan_rcr_does_not_exist_9f3a");
  (void)original.open();
  RCR_EXPECT(original.native_handle() == -1);

  rcr::SocketCan moved(std::move(original));
  RCR_EXPECT(moved.native_handle() == -1);
  RCR_EXPECT(original.native_handle() == -1);  // NOLINT(bugprone-use-after-move)
}

RCR_TEST_MAIN()
