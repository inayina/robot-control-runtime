// Linux 目标测试；不依赖 vcan 或硬件。覆盖节点业务状态，不替代进程间验收。
#include "rcr/can_v1.hpp"
#include "rcr/node_sim.hpp"
#include "test_support.hpp"

namespace {

rcr::CanFrame make_command(std::uint8_t node, std::uint8_t mask,
                           std::uint16_t session, std::uint16_t sequence,
                           std::uint8_t values, std::uint8_t validity_10ms) {
  rcr::can_v1::WireOutputCommand cmd{};
  cmd.node_id = node;
  cmd.mask = mask;
  cmd.session_id = session;
  cmd.sequence = sequence;
  cmd.values = values;
  cmd.validity_10ms = validity_10ms;
  auto encoded = rcr::can_v1::encode_output_command(cmd);
  RCR_REQUIRE(encoded.ok());
  return encoded.value();
}

}  // namespace

RCR_TEST(NodeAppliesFreshCommand) {
  rcr::CanNodeLogic::Config config{};
  config.node_id = 1;
  config.session_id = 10;
  rcr::CanNodeLogic node(config);

  const auto frame = make_command(1, 0x0F, 10, 1, 0x05, 10);
  const auto result = node.on_frame(frame, /*now_ns=*/1'000);
  RCR_EXPECT(result.send_status);
  RCR_EXPECT(result.status.result == rcr::can_v1::OutputResult::Applied);
  RCR_EXPECT(result.status.output_mirror == 0x05);
  RCR_EXPECT(node.output_bits() == 0x05);
  RCR_EXPECT(node.output_lease_active());
  RCR_EXPECT(node.output_lease_deadline_ns() == 100'001'000);
  RCR_EXPECT(node.last_accepted_sequence() == 1);
}

RCR_TEST(AppliedOutputHoldsWithinLeaseAndExpiresAtDeadline) {
  rcr::CanNodeLogic node({});
  const auto applied = node.on_frame(make_command(1, 0xFF, 1, 1, 0xA5, 1), 0);
  RCR_REQUIRE(applied.status.result == rcr::can_v1::OutputResult::Applied);
  RCR_EXPECT(!node.expire_output_lease(9'999'999));
  RCR_EXPECT(node.output_bits() == 0xA5);

  RCR_EXPECT(node.expire_output_lease(10'000'000));
  RCR_EXPECT(node.output_bits() == 0);
  RCR_EXPECT(!node.output_lease_active());
}

RCR_TEST(NewAppliedCommandRefreshesOutputLease) {
  rcr::CanNodeLogic node({});
  RCR_REQUIRE(node.on_frame(make_command(1, 0xFF, 1, 1, 0xA0, 10), 0)
                  .status.result == rcr::can_v1::OutputResult::Applied);
  RCR_REQUIRE(node.on_frame(make_command(1, 0x0F, 1, 2, 0x05, 10), 50'000'000)
                  .status.result == rcr::can_v1::OutputResult::Applied);
  RCR_EXPECT(node.output_bits() == 0xA5);
  RCR_EXPECT(node.output_lease_deadline_ns() == 150'000'000);
  RCR_EXPECT(!node.expire_output_lease(100'000'000));
  RCR_EXPECT(node.output_bits() == 0xA5);
  RCR_EXPECT(node.expire_output_lease(150'000'000));
  RCR_EXPECT(node.output_bits() == 0);
}

RCR_TEST(RejectedCommandDoesNotRefreshOutputLease) {
  rcr::CanNodeLogic node({});
  RCR_REQUIRE(node.on_frame(make_command(1, 1, 1, 5, 1, 10), 0)
                  .status.result == rcr::can_v1::OutputResult::Applied);
  const auto stale =
      node.on_frame(make_command(1, 1, 1, 5, 1, 20), 90'000'000);
  RCR_EXPECT(stale.status.result == rcr::can_v1::OutputResult::StaleSequence);
  RCR_EXPECT(node.output_lease_deadline_ns() == 100'000'000);
  RCR_EXPECT(node.expire_output_lease(100'000'000));
  RCR_EXPECT(node.output_bits() == 0);
}

RCR_TEST(InterlockLossImmediatelyNeutralizesOutput) {
  rcr::CanNodeLogic node({});
  RCR_REQUIRE(node.on_frame(make_command(1, 0xFF, 1, 1, 0xA5, 10), 0)
                  .status.result == rcr::can_v1::OutputResult::Applied);
  node.set_interlock_ready(false);
  RCR_EXPECT(node.output_bits() == 0);
  RCR_EXPECT(!node.output_lease_active());

  node.set_interlock_ready(true);
  RCR_EXPECT(node.output_bits() == 0);
}

RCR_TEST(NodeRejectsStaleAndDuplicateSequence) {
  rcr::CanNodeLogic::Config config{};
  config.session_id = 1;
  rcr::CanNodeLogic node(config);

  RCR_REQUIRE(node.on_frame(make_command(1, 1, 1, 5, 1, 10), 0).send_status);
  const auto dup = node.on_frame(make_command(1, 1, 1, 5, 1, 10), 0);
  RCR_EXPECT(dup.status.result == rcr::can_v1::OutputResult::StaleSequence);
  const auto older = node.on_frame(make_command(1, 1, 1, 4, 1, 10), 0);
  RCR_EXPECT(older.status.result == rcr::can_v1::OutputResult::StaleSequence);
  const auto newer = node.on_frame(make_command(1, 1, 1, 6, 1, 10), 0);
  RCR_EXPECT(newer.status.result == rcr::can_v1::OutputResult::Applied);
}

RCR_TEST(NodeRejectsSessionMismatchAndExpired) {
  rcr::CanNodeLogic::Config config{};
  config.session_id = 7;
  rcr::CanNodeLogic node(config);

  const auto mismatch = node.on_frame(make_command(1, 1, 8, 1, 1, 10), 0);
  RCR_EXPECT(mismatch.status.result == rcr::can_v1::OutputResult::SessionMismatch);
  RCR_EXPECT(node.output_bits() == 0);

  rcr::can_v1::WireOutputCommand cmd{};
  cmd.node_id = 1;
  cmd.mask = 1;
  cmd.session_id = 7;
  cmd.sequence = 1;
  cmd.values = 1;
  cmd.validity_10ms = 1;  // 10 ms
  // 接收于 t=0，延迟应用到 t=10ms → 刚好过期。
  const auto expired = node.apply_command(cmd, /*receive=*/0, /*now=*/10'000'000);
  RCR_EXPECT(expired.status.result == rcr::can_v1::OutputResult::Expired);
}

RCR_TEST(NodeNotReadyWhenInterlockOpen) {
  rcr::CanNodeLogic::Config config{};
  config.interlock_ready = false;
  rcr::CanNodeLogic node(config);
  const auto result = node.on_frame(make_command(1, 1, 1, 1, 1, 10), 0);
  RCR_EXPECT(result.status.result == rcr::can_v1::OutputResult::NotReady);
  RCR_EXPECT(node.output_bits() == 0);
}

RCR_TEST(SoftRestartInvalidatesOldSessionAndSequence) {
  rcr::CanNodeLogic::Config config{};
  config.boot_id = 1;
  config.session_id = 1;
  rcr::CanNodeLogic node(config);

  RCR_REQUIRE(
      node.on_frame(make_command(1, 0xFF, 1, 3, 0xAB, 10), 0).status.result ==
      rcr::can_v1::OutputResult::Applied);
  RCR_EXPECT(node.output_bits() == 0xAB);

  const auto old_session = node.session_id();
  node.soft_restart();
  RCR_EXPECT(node.boot_id() == 2);
  RCR_EXPECT(node.session_id() != old_session);
  RCR_EXPECT(node.output_bits() == 0);
  RCR_EXPECT(!node.output_lease_active());
  RCR_EXPECT(!node.has_accepted_sequence());

  const auto stale_session =
      node.on_frame(make_command(1, 1, old_session, 4, 1, 10), 0);
  RCR_EXPECT(stale_session.status.result ==
             rcr::can_v1::OutputResult::SessionMismatch);

  const auto fresh = node.on_frame(
      make_command(1, 1, node.session_id(), 1, 1, 10), 0);
  RCR_EXPECT(fresh.status.result == rcr::can_v1::OutputResult::Applied);
}

RCR_TEST(ProtocolRejectOnIllegalCommandFrame) {
  rcr::CanNodeLogic node({});
  rcr::CanFrame bad{};
  bad.can_id = rcr::can_v1::make_can_id(rcr::can_v1::Function::OutputCommand, 1);
  bad.len = 8;
  bad.data[0] = 1;
  bad.data[1] = 0;  // zero mask → decode reject
  bad.data[2] = 0;
  bad.data[3] = 1;
  bad.data[4] = 0;
  bad.data[5] = 1;
  bad.data[7] = 10;

  const auto result = node.on_frame(bad, 0);
  RCR_EXPECT(!result.send_status);
  RCR_EXPECT(result.protocol_reject);
  RCR_EXPECT(node.protocol_rejects() == 1);
}

RCR_TEST(IgnoresForeignNodeAndNonCommandFrames) {
  rcr::CanNodeLogic node({});
  const auto foreign = make_command(2, 1, 1, 1, 1, 10);
  const auto ignored = node.on_frame(foreign, 0);
  RCR_EXPECT(!ignored.send_status);
  RCR_EXPECT(!ignored.protocol_reject);

  auto hb = rcr::can_v1::encode_heartbeat(node.make_heartbeat());
  RCR_REQUIRE(hb.ok());
  // 自己的 heartbeat ID 不应被当成命令处理。
  const auto again = node.on_frame(hb.value(), 0);
  RCR_EXPECT(!again.send_status);
}

RCR_TEST(HeartbeatAndStatusWireFields) {
  rcr::CanNodeLogic::Config config{};
  config.node_id = 3;
  config.boot_id = 9;
  config.session_id = 11;
  config.interlock_ready = true;
  config.input_bits = 0x1234;
  config.fault_code = 4;
  rcr::CanNodeLogic node(config);

  const auto hb0 = node.make_heartbeat();
  RCR_EXPECT(hb0.node_id == 3);
  RCR_EXPECT(hb0.boot_id == 9);
  RCR_EXPECT(hb0.session_id == 11);
  RCR_EXPECT(hb0.hb_seq == 0);
  const auto hb1 = node.make_heartbeat();
  RCR_EXPECT(hb1.hb_seq == 1);

  const auto st = node.make_status();
  RCR_EXPECT(st.interlock_ready);
  RCR_EXPECT(st.input_bits == 0x1234);
  RCR_EXPECT(st.fault_code == 4);
  RCR_EXPECT(st.session_id == 11);
}

RCR_TEST(MaskPartialUpdatePreservesOtherBits) {
  rcr::CanNodeLogic node({});
  RCR_REQUIRE(node.on_frame(make_command(1, 0x0F, 1, 1, 0x0A, 10), 0)
                  .status.result == rcr::can_v1::OutputResult::Applied);
  RCR_EXPECT(node.output_bits() == 0x0A);
  RCR_REQUIRE(node.on_frame(make_command(1, 0xF0, 1, 2, 0xB0, 10), 0)
                  .status.result == rcr::can_v1::OutputResult::Applied);
  RCR_EXPECT(node.output_bits() == 0xBA);
}

RCR_TEST_MAIN()
