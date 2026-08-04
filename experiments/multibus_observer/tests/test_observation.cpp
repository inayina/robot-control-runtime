#include "rcr_multibus/can_status_adapter.hpp"
#include "rcr_multibus/modbus_temperature_adapter.hpp"
#include "rcr_multibus/observation.hpp"

#include "rcr/can_v1.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

#define CHECK(expr)                                                                      \
  do {                                                                                   \
    if (!(expr)) {                                                                       \
      std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << " — " #expr   \
                << "\n";                                                               \
      return 1;                                                                          \
    }                                                                                    \
  } while (false)

}  // namespace

int main() {
  using namespace rcr::multibus;

  ObservationStore store;
  auto initial = store.snapshot();
  CHECK(initial.can_source.health == SourceHealth::Waiting);
  CHECK(initial.modbus_source.health == SourceHealth::Waiting);
  CHECK(!initial.can.valid);
  CHECK(!initial.temperature.valid);

  rcr::can_v1::WireNodeStatus wire{};
  wire.node_id = 1;
  wire.session_id = 7;
  wire.interlock_ready = true;
  wire.input_bits = 0x0021;
  wire.fault_code = 0;
  const auto frame = rcr::can_v1::encode_node_status(wire);
  CHECK(frame);
  CHECK(ingest_can_frame(store, frame.value(), 1, 1'000'000'000LL) ==
        CanIngestResult::Updated);

  auto snapshot = store.snapshot();
  CHECK(snapshot.can.valid);
  CHECK(snapshot.can.node_id == 1);
  CHECK(snapshot.can.session_id == 7);
  CHECK(snapshot.can.interlock_ready);
  CHECK(snapshot.can.input_bits == 0x0021);
  CHECK(snapshot.can_source.health == SourceHealth::Healthy);
  CHECK(snapshot.can_source.updates == 1);

  rcr::can_v1::WireHeartbeat heartbeat{};
  heartbeat.node_id = 1;
  heartbeat.boot_id = 1;
  heartbeat.session_id = 7;
  heartbeat.hb_seq = 9;
  const auto heartbeat_frame = rcr::can_v1::encode_heartbeat(heartbeat);
  CHECK(heartbeat_frame);
  CHECK(ingest_can_frame(store, heartbeat_frame.value(), 1, 1'010'000'000LL) ==
        CanIngestResult::Ignored);
  CHECK(store.snapshot().can_source.updates == 1);

  rcr::CanFrame invalid{};
  invalid.can_id = 0x7FF;
  invalid.len = 1;
  CHECK(ingest_can_frame(store, invalid, 1, 1'020'000'000LL) ==
        CanIngestResult::Rejected);
  snapshot = store.snapshot();
  CHECK(snapshot.can.valid);  // 最后好值仍可用于诊断
  CHECK(snapshot.can_source.health == SourceHealth::Faulted);
  CHECK(snapshot.can_source.failures == 1);

  const auto positive = decode_temperature_register(0x00FF, 0, 2'000'000'000LL);
  CHECK(positive.valid);
  CHECK(positive.deci_celsius == 255);
  store.publish_temperature(positive);
  snapshot = store.snapshot();
  CHECK(snapshot.temperature.deci_celsius == 255);
  CHECK(snapshot.modbus_source.health == SourceHealth::Healthy);

  const auto negative = decode_temperature_register(0xFF9C, 4, 2'100'000'000LL);
  CHECK(negative.deci_celsius == -100);

  store.mark_modbus_failure(2'200'000'000LL, "response timeout");
  snapshot = store.snapshot();
  CHECK(snapshot.temperature.valid);
  CHECK(snapshot.temperature.deci_celsius == 255);
  CHECK(snapshot.modbus_source.health == SourceHealth::Faulted);
  CHECK(snapshot.modbus_source.failures == 1);
  CHECK(snapshot.modbus_source.detail == "response timeout");

  CHECK(!sample_is_stale(true, 3'000'000'000LL, 3'050'000'000LL, 100'000'000LL));
  CHECK(sample_is_stale(true, 3'000'000'000LL, 3'100'000'001LL, 100'000'000LL));
  CHECK(sample_is_stale(false, 0, 3'100'000'001LL, 100'000'000LL));

  std::cout << "test_observation PASS\n";
  return 0;
}
