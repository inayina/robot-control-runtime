#include "check.hpp"
#include "rcr_mbus/codec.hpp"
#include "rcr_mbus/framing.hpp"

#include <vector>

using namespace rcr::mbus;

int main() {
  Adu a;
  a.mbap.transaction_id = 1;
  a.mbap.unit_id = 1;
  a.pdu = encode_read_holding_request(0, 1).value;
  auto f1 = encode_adu(a).value;

  Adu b = a;
  b.mbap.transaction_id = 2;
  b.pdu = encode_write_single_request(1, 0x55).value;
  auto f2 = encode_adu(b).value;

  // 半包：先喂 MBAP 前 4 字节
  StreamFramer fr;
  fr.append(std::span<const std::uint8_t>(f1.data(), 4));
  auto r = fr.try_pop_adu();
  CHECK(r.error == Error::NeedMore);

  // 再喂完第一帧 + 第二帧前半
  fr.append(std::span<const std::uint8_t>(f1.data() + 4, f1.size() - 4));
  fr.append(std::span<const std::uint8_t>(f2.data(), 3));
  r = fr.try_pop_adu();
  CHECK(r);
  CHECK(r.value == f1);
  r = fr.try_pop_adu();
  CHECK(r.error == Error::NeedMore);

  fr.append(std::span<const std::uint8_t>(f2.data() + 3, f2.size() - 3));
  r = fr.try_pop_adu();
  CHECK(r);
  CHECK(r.value == f2);

  // 非法 length（过大）
  StreamFramer bad;
  const std::uint8_t oversize_hdr[] = {0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01};
  bad.append(oversize_hdr);
  r = bad.try_pop_adu();
  CHECK(r.error == Error::InvalidLength);

  // 非法 protocol id
  StreamFramer bad_pid;
  const std::uint8_t badp[] = {0x00, 0x01, 0x00, 0x01, 0x00, 0x06, 0x01, 0x03,
                               0x00, 0x00, 0x00, 0x01};
  bad_pid.append(badp);
  r = bad_pid.try_pop_adu();
  CHECK(r.error == Error::InvalidProtocolId);

  return 0;
}
