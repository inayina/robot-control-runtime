#include "check.hpp"
#include "rcr_mbus/codec.hpp"

#include <vector>

using namespace rcr::mbus;

static void test_read_holding_roundtrip() {
  auto pdu = encode_read_holding_request(0x0010, 3);
  CHECK(pdu);
  CHECK(pdu.value.size() == 5);
  CHECK(pdu.value[0] == kFcReadHolding);

  Adu adu;
  adu.mbap.transaction_id = 0x000A;
  adu.mbap.protocol_id = 0;
  adu.mbap.unit_id = 1;
  adu.pdu = pdu.value;
  auto frame = encode_adu(adu);
  CHECK(frame);
  // Golden: TID=000A PID=0000 LEN=0006 UNIT=01 FC=03 ADDR=0010 QTY=0003
  const std::vector<std::uint8_t> golden = {0x00, 0x0A, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03,
                                            0x00, 0x10, 0x00, 0x03};
  CHECK_MSG(frame.value == golden, "read holding ADU golden mismatch");

  auto decoded = decode_adu(frame.value);
  CHECK(decoded);
  CHECK(decoded.value.mbap.transaction_id == 0x000A);
  CHECK(decoded.value.pdu == pdu.value);
}

static void test_exception_encode() {
  auto pdu = encode_exception_response(kFcReadHolding, kExIllegalDataAddress);
  CHECK(pdu);
  CHECK(pdu.value.size() == 2);
  CHECK(pdu.value[0] == 0x83);
  CHECK(pdu.value[1] == kExIllegalDataAddress);
  auto ex = decode_exception_response(pdu.value);
  CHECK(ex);
  CHECK(ex.value.function == kFcReadHolding);
  CHECK(ex.value.code == kExIllegalDataAddress);
}

static void test_write_multiple() {
  const std::uint16_t vals[] = {0x0001, 0x0002};
  auto pdu = encode_write_multiple_request(5, vals);
  CHECK(pdu);
  CHECK(pdu.value[0] == kFcWriteMultiple);
  auto resp = encode_write_multiple_response(5, 2);
  CHECK(resp);
  auto d = decode_write_multiple_response(resp.value);
  CHECK(d);
  CHECK(d.value.address == 5);
  CHECK(d.value.quantity == 2);
}

int main() {
  test_read_holding_roundtrip();
  test_exception_encode();
  test_write_multiple();
  return 0;
}
