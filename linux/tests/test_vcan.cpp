// Linux 目标测试；不依赖 MCU 工具链或硬件烧录环境。
#include "rcr/vcan.hpp"
#include "test_support.hpp"

RCR_TEST(DefaultNameIsVcan0) {
  RCR_EXPECT(rcr::default_vcan_name() == "vcan0");
}

RCR_TEST(RejectInvalidNames) {
  RCR_EXPECT(rcr::probe_can_interface("../etc") == rcr::CanInterfaceStatus::InvalidName);
  RCR_EXPECT(rcr::probe_can_interface("") == rcr::CanInterfaceStatus::InvalidName);
  RCR_EXPECT(rcr::probe_can_interface("vcan0;rm") == rcr::CanInterfaceStatus::InvalidName);
  RCR_EXPECT(rcr::probe_can_interface("a b") == rcr::CanInterfaceStatus::InvalidName);
  RCR_EXPECT(!rcr::net_interface_exists("../etc"));
  RCR_EXPECT(!rcr::net_interface_exists(""));
  RCR_EXPECT(!rcr::can_interface_available("vcan0;rm"));
}

RCR_TEST(MissingInterfaceIsMissingNotAvailable) {
  // 接口名须落在内核 IFNAMSIZ 白名单内，否则会先被判为 InvalidName。
  const auto status = rcr::probe_can_interface("vcan_miss_zz");
  RCR_EXPECT(status == rcr::CanInterfaceStatus::Missing);
  RCR_EXPECT(!rcr::can_interface_available("vcan_miss_zz"));
  RCR_EXPECT(!rcr::net_interface_exists("vcan_miss_zz"));
}

RCR_TEST(LoIsNotReportedAsCan) {
  // lo 几乎总是存在且 type 不是 ARPHRD_CAN；用来锁住“同名 net != CAN”语义。
  if (!rcr::net_interface_exists("lo")) {
    RCR_SKIP("lo interface not present");
  }
  const auto status = rcr::probe_can_interface("lo");
  RCR_EXPECT(status == rcr::CanInterfaceStatus::NotCan);
  RCR_EXPECT(!rcr::can_interface_available("lo"));
}

RCR_TEST(ProbeDoesNotThrow) {
  (void)rcr::probe_can_interface("vcan0");
  (void)rcr::can_interface_available("vcan0");
}

RCR_TEST_MAIN()
