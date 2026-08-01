// Linux 目标测试；不依赖 MCU 工具链或硬件烧录环境。
#include "rcr/can_bus.hpp"
#include "test_support.hpp"

using rcr::CanFrame;
using rcr::Errc;
using rcr::FakeCanBus;

RCR_TEST(FakeCanLoopback) {
  FakeCanBus bus("fake0");
  RCR_EXPECT(!bus.is_open());
  RCR_REQUIRE(bus.open().ok());
  RCR_EXPECT(bus.is_open());
  RCR_EXPECT(bus.interface_name() == "fake0");

  CanFrame tx{};
  tx.can_id = 0x123;
  tx.len = 2;
  tx.data[0] = 0xAB;
  tx.data[1] = 0xCD;

  RCR_REQUIRE(bus.send(tx).ok());
  RCR_EXPECT(bus.queued() == 1);

  auto rx = bus.receive(std::chrono::milliseconds{0});
  RCR_REQUIRE(rx.ok());
  RCR_EXPECT(rx.value().can_id == 0x123);
  RCR_EXPECT(rx.value().len == 2);
  RCR_EXPECT(rx.value().data[0] == 0xAB);
  RCR_EXPECT(rx.value().data[1] == 0xCD);
  RCR_EXPECT(bus.queued() == 0);
}

RCR_TEST(FakeCanNotOpenErrors) {
  FakeCanBus bus;
  auto send_rc = bus.send(CanFrame{});
  RCR_EXPECT(!send_rc.ok());
  RCR_EXPECT(send_rc.error().code() == Errc::NotOpen);

  auto recv_rc = bus.receive(std::chrono::milliseconds{0});
  RCR_EXPECT(!recv_rc.ok());
  RCR_EXPECT(recv_rc.error().code() == Errc::NotOpen);
}

RCR_TEST(FakeCanRejectsOversize) {
  FakeCanBus bus;
  RCR_REQUIRE(bus.open().ok());
  CanFrame bad{};
  bad.len = 9;
  auto rc = bus.send(bad);
  RCR_EXPECT(!rc.ok());
  RCR_EXPECT(rc.error().code() == Errc::InvalidArgument);
}

RCR_TEST(FakeCanEmptyReceiveTimesOut) {
  FakeCanBus bus;
  RCR_REQUIRE(bus.open().ok());
  auto rc = bus.receive(std::chrono::milliseconds{1});
  RCR_EXPECT(!rc.ok());
  RCR_EXPECT(rc.error().code() == Errc::Timeout);
}

RCR_TEST(ICanBusPolymorphism) {
  FakeCanBus concrete;
  rcr::ICanBus& bus = concrete;
  RCR_REQUIRE(bus.open().ok());
  CanFrame frame{};
  frame.can_id = 0x42;
  frame.len = 1;
  frame.data[0] = 0x11;
  RCR_REQUIRE(bus.send(frame).ok());
  auto rx = bus.receive(std::chrono::milliseconds{0});
  RCR_REQUIRE(rx.ok());
  RCR_EXPECT(rx.value().can_id == 0x42);
}

RCR_TEST_MAIN()
