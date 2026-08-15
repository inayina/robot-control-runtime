#include "rcr/workbench/services/modbus_rtu.hpp"

#include "test_support.hpp"

#include <string>
#include <vector>

namespace {

using rcr::workbench::decode_rtu_read_bits;
using rcr::workbench::decode_rtu_write_single_coil;
using rcr::workbench::encode_rtu_read;
using rcr::workbench::encode_rtu_write_single_coil;
using rcr::workbench::kModbusFnReadDiscreteInputs;

RCR_TEST(encode_matches_live_fc02_probe) {
  const auto tx = encode_rtu_read(1, kModbusFnReadDiscreteInputs, 0, 8);
  RCR_EXPECT(rcr::workbench::bytes_to_hex(tx) == "01020000000879cc");
}

RCR_TEST(decode_matches_live_fc02_reply) {
  const std::vector<std::uint8_t> rx{0x01, 0x02, 0x01, 0x00, 0xa1, 0x88};
  const auto decoded = decode_rtu_read_bits(rx, 1, kModbusFnReadDiscreteInputs);
  RCR_REQUIRE(decoded.ok());
  RCR_EXPECT(!decoded.value().exception);
  RCR_EXPECT(decoded.value().bit_byte == 0x00);
}

RCR_TEST(bad_crc_is_rejected) {
  const std::vector<std::uint8_t> rx{0x01, 0x02, 0x01, 0x00, 0x00, 0x00};
  const auto decoded = decode_rtu_read_bits(rx, 1, kModbusFnReadDiscreteInputs);
  RCR_EXPECT(!decoded.ok());
}

RCR_TEST(fc05_round_trip_echo) {
  const auto tx = encode_rtu_write_single_coil(1, 0, true);
  RCR_EXPECT(tx.size() == 8);
  RCR_EXPECT(tx[1] == 0x05);
  RCR_EXPECT(tx[4] == 0xFF);
  const auto decoded = decode_rtu_write_single_coil(tx, 1, 0, true);
  RCR_REQUIRE(decoded.ok());
  RCR_EXPECT(decoded.value().on);
  const auto off = encode_rtu_write_single_coil(1, 2, false);
  const auto decoded_off = decode_rtu_write_single_coil(off, 1, 2, false);
  RCR_REQUIRE(decoded_off.ok());
  RCR_EXPECT(!decoded_off.value().on);
}

} // namespace

RCR_TEST_MAIN()
