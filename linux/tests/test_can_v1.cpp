// Linux 目标测试；不依赖 MCU 工具链、vcan 或硬件。
// golden vectors 与 protocol/can_v1/golden_vectors.tsv / README §10 对齐。
#include "rcr/can_v1.hpp"
#include "test_support.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace {

constexpr std::uint32_t kCanEffFlag = 0x80000000u;
constexpr std::uint32_t kCanRtrFlag = 0x40000000u;

rcr::CanFrame frame_from(std::uint32_t can_id, std::uint8_t dlc,
                         std::string_view data_hex) {
  rcr::CanFrame frame{};
  frame.can_id = can_id;
  frame.len = dlc;
  std::memset(frame.data, 0, sizeof(frame.data));
  RCR_REQUIRE(data_hex.size() % 2 == 0);
  RCR_REQUIRE(data_hex.size() / 2 <= 8);
  for (std::size_t i = 0; i < data_hex.size(); i += 2) {
    const auto nibble = [](char c) -> int {
      if (c >= '0' && c <= '9') {
        return c - '0';
      }
      if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
      }
      if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
      }
      return -1;
    };
    const int hi = nibble(data_hex[i]);
    const int lo = nibble(data_hex[i + 1]);
    RCR_REQUIRE(hi >= 0 && lo >= 0);
    frame.data[i / 2] = static_cast<std::uint8_t>((hi << 4) | lo);
  }
  return frame;
}

bool data_equals(const rcr::CanFrame& frame, std::string_view data_hex) {
  if (frame.len != 8 || data_hex.size() != 16) {
    return false;
  }
  const auto expected = frame_from(frame.can_id, 8, data_hex);
  return std::memcmp(frame.data, expected.data, 8) == 0;
}

}  // namespace

RCR_TEST(SeqNewerWrapArithmetic) {
  using rcr::can_v1::seq_newer;
  RCR_EXPECT(seq_newer(2, 1));
  RCR_EXPECT(!seq_newer(1, 1));
  RCR_EXPECT(!seq_newer(1, 2));
  RCR_EXPECT(seq_newer(1, 65535));
  RCR_EXPECT(!seq_newer(65535, 1));
  RCR_EXPECT(seq_newer(0, 65535));  // heartbeat 允许回绕到 0
}

RCR_TEST(ValidityDeadlineConversion) {
  using rcr::can_v1::deadline_from_validity_10ms;
  using rcr::can_v1::validity_10ms_from_deadline;

  RCR_EXPECT(deadline_from_validity_10ms(1000, 1) == 1000 + 10'000'000LL);
  RCR_EXPECT(deadline_from_validity_10ms(0, 250) == 2'500'000'000LL);

  const auto ok = validity_10ms_from_deadline(0, 100'000'000LL);  // 100 ms
  RCR_REQUIRE(ok.ok());
  RCR_EXPECT(ok.value() == 10);

  const auto ceil = validity_10ms_from_deadline(0, 15'000'001LL);  // >15ms
  RCR_REQUIRE(ceil.ok());
  RCR_EXPECT(ceil.value() == 2);

  RCR_EXPECT(!validity_10ms_from_deadline(0, 9'999'999LL).ok());
  RCR_EXPECT(!validity_10ms_from_deadline(0, 2'500'000'001LL).ok());
}

RCR_TEST(GoldenHeartbeatRoundTrip) {
  using namespace rcr::can_v1;
  const auto frame = frame_from(0x021, 8, "0100000100010000");
  const auto decoded = decode_heartbeat(frame);
  RCR_REQUIRE(decoded.ok());
  RCR_EXPECT(decoded.value().node_id == 1);
  RCR_EXPECT(decoded.value().boot_id == 1);
  RCR_EXPECT(decoded.value().session_id == 1);
  RCR_EXPECT(decoded.value().hb_seq == 0);

  const auto encoded = encode_heartbeat(decoded.value());
  RCR_REQUIRE(encoded.ok());
  RCR_EXPECT(encoded.value().can_id == 0x021);
  RCR_EXPECT(encoded.value().len == 8);
  RCR_EXPECT(data_equals(encoded.value(), "0100000100010000"));
}

RCR_TEST(GoldenHeartbeatTypicalAndWrap) {
  using namespace rcr::can_v1;
  {
    const auto d = decode_heartbeat(frame_from(0x021, 8, "01000002000A0100"));
    RCR_REQUIRE(d.ok());
    RCR_EXPECT(d.value().boot_id == 2);
    RCR_EXPECT(d.value().session_id == 10);
    RCR_EXPECT(d.value().hb_seq == 256);
    const auto e = encode_heartbeat(d.value());
    RCR_REQUIRE(e.ok());
    RCR_EXPECT(data_equals(e.value(), "01000002000A0100"));
  }
  {
    const auto d = decode_heartbeat(frame_from(0x021, 8, "010000010001FFFF"));
    RCR_REQUIRE(d.ok());
    RCR_EXPECT(d.value().hb_seq == 65535);
  }
}

RCR_TEST(GoldenHeartbeatRejects) {
  using namespace rcr::can_v1;
  RCR_EXPECT(!decode_heartbeat(frame_from(0x021, 8, "0200000100010001")).ok());
  RCR_EXPECT(!decode_heartbeat(frame_from(0x021, 8, "0101000100010001")).ok());
  RCR_EXPECT(!decode_heartbeat(frame_from(0x021, 8, "0100000100000001")).ok());
  RCR_EXPECT(!decode_heartbeat(frame_from(0x021, 8, "0100000000010001")).ok());

  rcr::CanFrame rtr = frame_from(0x021, 8, "0100000100010000");
  rtr.can_id |= kCanRtrFlag;
  rtr.len = 0;
  RCR_EXPECT(!decode_heartbeat(rtr).ok());

  rcr::CanFrame ext = frame_from(0x021, 8, "0100000100010000");
  ext.can_id |= kCanEffFlag;
  ext.len = 0;
  RCR_EXPECT(!decode_heartbeat(ext).ok());

  RCR_EXPECT(!decode_heartbeat(frame_from(0x021, 7, "01000001000100")).ok());
}

RCR_TEST(GoldenNodeStatusRoundTrip) {
  using namespace rcr::can_v1;
  const auto d = decode_node_status(frame_from(0x041, 8, "0101000A00030006"));
  RCR_REQUIRE(d.ok());
  RCR_EXPECT(d.value().interlock_ready);
  RCR_EXPECT(d.value().session_id == 10);
  RCR_EXPECT(d.value().input_bits == 3);
  RCR_EXPECT(d.value().fault_code == 6);
  const auto e = encode_node_status(d.value());
  RCR_REQUIRE(e.ok());
  RCR_EXPECT(e.value().can_id == 0x041);
  RCR_EXPECT(data_equals(e.value(), "0101000A00030006"));

  RCR_EXPECT(decode_node_status(frame_from(0x041, 8, "0100000100000000")).ok());
  RCR_EXPECT(decode_node_status(frame_from(0x041, 8, "01010001FFFF0000")).ok());
  RCR_EXPECT(!decode_node_status(frame_from(0x041, 8, "0102000100000000")).ok());
  RCR_EXPECT(!decode_node_status(frame_from(0x041, 8, "0101000000000000")).ok());
}

RCR_TEST(GoldenOutputCommandRoundTrip) {
  using namespace rcr::can_v1;
  const auto d = decode_output_command(frame_from(0x061, 8, "010F000A0005050A"));
  RCR_REQUIRE(d.ok());
  RCR_EXPECT(d.value().mask == 0x0F);
  RCR_EXPECT(d.value().session_id == 10);
  RCR_EXPECT(d.value().sequence == 5);
  RCR_EXPECT(d.value().values == 5);
  RCR_EXPECT(d.value().validity_10ms == 10);
  const auto e = encode_output_command(d.value());
  RCR_REQUIRE(e.ok());
  RCR_EXPECT(data_equals(e.value(), "010F000A0005050A"));

  RCR_EXPECT(decode_output_command(frame_from(0x061, 8, "0101000100010101")).ok());
  RCR_EXPECT(decode_output_command(frame_from(0x061, 8, "01010001000201FA")).ok());
  RCR_EXPECT(decode_output_command(frame_from(0x061, 8, "01010001FFFF010A")).ok());
}

RCR_TEST(GoldenOutputCommandRejects) {
  using namespace rcr::can_v1;
  RCR_EXPECT(!decode_output_command(frame_from(0x061, 8, "010000010001010A")).ok());
  RCR_EXPECT(!decode_output_command(frame_from(0x061, 8, "010100010000010A")).ok());
  RCR_EXPECT(!decode_output_command(frame_from(0x061, 8, "010100000001010A")).ok());
  RCR_EXPECT(!decode_output_command(frame_from(0x061, 8, "0101000100010100")).ok());
  RCR_EXPECT(!decode_output_command(frame_from(0x061, 8, "01010001000101FB")).ok());

  WireOutputCommand bad{};
  bad.node_id = 1;
  bad.mask = 0;
  bad.session_id = 1;
  bad.sequence = 1;
  bad.values = 1;
  bad.validity_10ms = 10;
  RCR_EXPECT(!encode_output_command(bad).ok());
}

RCR_TEST(GoldenOutputStatusRoundTrip) {
  using namespace rcr::can_v1;
  const auto d = decode_output_status(frame_from(0x081, 8, "0100000A00050500"));
  RCR_REQUIRE(d.ok());
  RCR_EXPECT(d.value().result == OutputResult::Applied);
  RCR_EXPECT(d.value().session_id == 10);
  RCR_EXPECT(d.value().sequence == 5);
  RCR_EXPECT(d.value().output_mirror == 0x05);
  const auto e = encode_output_status(d.value());
  RCR_REQUIRE(e.ok());
  RCR_EXPECT(data_equals(e.value(), "0100000A00050500"));

  RCR_EXPECT(decode_output_status(frame_from(0x081, 8, "0101000A00050400")).ok());
  RCR_EXPECT(decode_output_status(frame_from(0x081, 8, "0102000B00050000")).ok());
  RCR_EXPECT(decode_output_status(frame_from(0x081, 8, "0103000A00050000")).ok());
  RCR_EXPECT(!decode_output_status(frame_from(0x081, 8, "0106000A00050000")).ok());
  RCR_EXPECT(!decode_output_status(frame_from(0x081, 8, "0100000A00050001")).ok());
  RCR_EXPECT(!decode_output_status(frame_from(0x081, 8, "0110000A00050000")).ok());
}

RCR_TEST(GoldenIdBoundaries) {
  using namespace rcr::can_v1;
  RCR_EXPECT(!decode(frame_from(0x020, 8, "0100000100010000")).ok());
  RCR_EXPECT(!decode(frame_from(0x0A1, 8, "0100000100010000")).ok());

  const auto ok = decode(frame_from(0x03F, 8, "0100000100010000"));
  RCR_REQUIRE(ok.ok());
  RCR_EXPECT(ok.value().kind == MessageKind::Heartbeat);
  RCR_EXPECT(ok.value().heartbeat.node_id == 31);
}

RCR_TEST(DecodeDispatchAndTruncatedFramesDoNotThrow) {
  using namespace rcr::can_v1;
  const auto cmd = decode(frame_from(0x061, 8, "0101000100010101"));
  RCR_REQUIRE(cmd.ok());
  RCR_EXPECT(cmd.value().kind == MessageKind::OutputCommand);

  for (std::uint8_t len = 0; len < 8; ++len) {
    rcr::CanFrame truncated{};
    truncated.can_id = 0x021;
    truncated.len = len;
    const auto rc = decode(truncated);
    RCR_EXPECT(!rc.ok());
    RCR_EXPECT(rc.error().code() == rcr::Errc::Rejected);
  }

  // 随机脏数据：只能得到成功或明确 Rejected，不能抛异常。
  rcr::CanFrame dirty{};
  dirty.can_id = 0x061;
  dirty.len = 8;
  for (int i = 0; i < 8; ++i) {
    dirty.data[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(0xA5u + i);
  }
  const auto dirty_rc = decode(dirty);
  RCR_EXPECT(!dirty_rc.ok() || dirty_rc.value().kind == MessageKind::OutputCommand);
}

RCR_TEST_MAIN()
