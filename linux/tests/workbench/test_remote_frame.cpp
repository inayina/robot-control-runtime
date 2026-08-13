#include "rcr/workbench/application/remote_frame.hpp"

#include "test_support.hpp"

#include <cstdint>
#include <vector>

namespace {

using rcr::workbench::RemoteFrame;
using rcr::workbench::RemoteFrameParseStatus;
using rcr::workbench::RemoteFrameParser;
using rcr::workbench::RemoteMessageType;
using rcr::workbench::encode_remote_frame;
using rcr::workbench::kRemoteFrameHeaderSize;
using rcr::workbench::kRemoteMaxPayloadSize;
using rcr::workbench::remote_crc16;

RemoteFrame make_frame(RemoteMessageType type, std::uint16_t sequence,
                       std::vector<std::uint8_t> payload) {
  RemoteFrame frame;
  frame.type = type;
  frame.sequence = sequence;
  frame.payload = std::move(payload);
  return frame;
}

RCR_TEST(encode_round_trip_single_frame) {
  std::vector<std::uint8_t> bytes;
  RCR_REQUIRE(encode_remote_frame(
      make_frame(RemoteMessageType::Hello, 7, {1, 2, 3}), bytes));

  RemoteFrameParser parser;
  RCR_EXPECT(parser.append(bytes) == RemoteFrameParseStatus::NeedMore);
  RemoteFrame out;
  RCR_REQUIRE(parser.try_pop(out) == RemoteFrameParseStatus::Ok);
  RCR_EXPECT(out.type == RemoteMessageType::Hello);
  RCR_EXPECT(out.sequence == 7);
  RCR_REQUIRE(out.payload.size() == 3);
  RCR_EXPECT(out.payload[0] == 1);
  RCR_EXPECT(out.payload[1] == 2);
  RCR_EXPECT(out.payload[2] == 3);
  RCR_EXPECT(parser.try_pop(out) == RemoteFrameParseStatus::NeedMore);
}

RCR_TEST(parser_handles_partial_then_complete_frame) {
  std::vector<std::uint8_t> bytes;
  RCR_REQUIRE(encode_remote_frame(
      make_frame(RemoteMessageType::GetStatus, 1, {}), bytes));

  RemoteFrameParser parser;
  const auto split = bytes.size() / 2;
  RCR_EXPECT(parser.append({bytes.data(), split}) ==
             RemoteFrameParseStatus::NeedMore);
  RemoteFrame out;
  RCR_EXPECT(parser.try_pop(out) == RemoteFrameParseStatus::NeedMore);
  RCR_EXPECT(parser.append({bytes.data() + split, bytes.size() - split}) ==
             RemoteFrameParseStatus::NeedMore);
  RCR_REQUIRE(parser.try_pop(out) == RemoteFrameParseStatus::Ok);
  RCR_EXPECT(out.type == RemoteMessageType::GetStatus);
}

RCR_TEST(parser_handles_two_frames_in_one_chunk) {
  std::vector<std::uint8_t> bytes;
  RCR_REQUIRE(encode_remote_frame(
      make_frame(RemoteMessageType::Hello, 1, {9}), bytes));
  RCR_REQUIRE(encode_remote_frame(
      make_frame(RemoteMessageType::Heartbeat, 2, {1, 2, 3, 4, 5, 6, 7, 8}),
      bytes));

  RemoteFrameParser parser;
  RCR_EXPECT(parser.append(bytes) == RemoteFrameParseStatus::NeedMore);
  RemoteFrame first;
  RemoteFrame second;
  RCR_REQUIRE(parser.try_pop(first) == RemoteFrameParseStatus::Ok);
  RCR_REQUIRE(parser.try_pop(second) == RemoteFrameParseStatus::Ok);
  RCR_EXPECT(first.sequence == 1);
  RCR_EXPECT(second.sequence == 2);
  RCR_EXPECT(first.type == RemoteMessageType::Hello);
  RCR_EXPECT(second.type == RemoteMessageType::Heartbeat);
}

RCR_TEST(invalid_magic_is_reported_and_resynced) {
  std::vector<std::uint8_t> bytes{0xDE, 0xAD};
  RCR_REQUIRE(encode_remote_frame(
      make_frame(RemoteMessageType::Hello, 3, {}), bytes));

  RemoteFrameParser parser;
  RCR_EXPECT(parser.append(bytes) == RemoteFrameParseStatus::NeedMore);
  RemoteFrame out;
  RCR_EXPECT(parser.try_pop(out) == RemoteFrameParseStatus::InvalidMagic);
  RCR_EXPECT(parser.try_pop(out) == RemoteFrameParseStatus::InvalidMagic);
  // 丢掉坏前缀后应能对齐到合法 magic。
  while (parser.try_pop(out) == RemoteFrameParseStatus::InvalidMagic) {
  }
  RCR_EXPECT(out.sequence == 3);
  RCR_EXPECT(out.type == RemoteMessageType::Hello);
}

RCR_TEST(unsupported_version_is_rejected) {
  std::vector<std::uint8_t> bytes;
  RCR_REQUIRE(encode_remote_frame(
      make_frame(RemoteMessageType::Hello, 1, {}), bytes));
  bytes[4] = 99;

  RemoteFrameParser parser;
  RCR_EXPECT(parser.append(bytes) == RemoteFrameParseStatus::NeedMore);
  RemoteFrame out;
  RCR_EXPECT(parser.try_pop(out) ==
             RemoteFrameParseStatus::UnsupportedVersion);
}

RCR_TEST(oversize_payload_is_rejected) {
  std::vector<std::uint8_t> bytes;
  RCR_REQUIRE(encode_remote_frame(
      make_frame(RemoteMessageType::Hello, 1, {}), bytes));
  // 把头里的 payload_len 改成超限，CRC 会坏；先测长度门，再单独测 CRC。
  bytes[10] = static_cast<std::uint8_t>((kRemoteMaxPayloadSize + 1) & 0xFF);
  bytes[11] =
      static_cast<std::uint8_t>(((kRemoteMaxPayloadSize + 1) >> 8) & 0xFF);

  RemoteFrameParser parser;
  RCR_EXPECT(parser.append(bytes) == RemoteFrameParseStatus::NeedMore);
  RemoteFrame out;
  RCR_EXPECT(parser.try_pop(out) == RemoteFrameParseStatus::OversizePayload);
}

RCR_TEST(bad_crc_is_rejected) {
  std::vector<std::uint8_t> bytes;
  RCR_REQUIRE(encode_remote_frame(
      make_frame(RemoteMessageType::Hello, 1, {1, 2}), bytes));
  bytes.back() ^= 0xFF;

  RemoteFrameParser parser;
  RCR_EXPECT(parser.append(bytes) == RemoteFrameParseStatus::NeedMore);
  RemoteFrame out;
  RCR_EXPECT(parser.try_pop(out) == RemoteFrameParseStatus::BadCrc);
}

RCR_TEST(buffer_overflow_clears_parser) {
  RemoteFrameParser parser;
  std::vector<std::uint8_t> chunk(rcr::workbench::kRemoteRxBufferCapacity, 0x11);
  RCR_EXPECT(parser.append(chunk) == RemoteFrameParseStatus::NeedMore);
  RCR_EXPECT(parser.append(std::vector<std::uint8_t>{0x22}) ==
             RemoteFrameParseStatus::BufferOverflow);
  RCR_EXPECT(parser.size() == 0);
}

RCR_TEST(crc_matches_known_empty_vector) {
  // 空输入的 CRC-16/IBM 初值即 0xFFFF。
  RCR_EXPECT(remote_crc16({}) == 0xFFFF);
  const std::uint8_t one[] = {0x00};
  RCR_EXPECT(remote_crc16(one) != 0xFFFF);
}

} // namespace

RCR_TEST_MAIN()
