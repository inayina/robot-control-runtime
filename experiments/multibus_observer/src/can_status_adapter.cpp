#include "rcr_multibus/can_status_adapter.hpp"

#include "rcr/can_v1.hpp"

namespace rcr::multibus {

CanIngestResult ingest_can_frame(ObservationStore& store, const CanFrame& frame,
                                 std::uint8_t expected_node_id,
                                 std::int64_t sampled_ns) {
  const auto decoded = can_v1::decode(frame);
  if (!decoded) {
    store.mark_can_failure(sampled_ns, decoded.error().message());
    return CanIngestResult::Rejected;
  }

  if (decoded.value().kind != can_v1::MessageKind::Status ||
      decoded.value().status.node_id != expected_node_id) {
    return CanIngestResult::Ignored;
  }

  const auto& status = decoded.value().status;
  CanStatusSample sample{};
  sample.sampled_ns = sampled_ns;
  sample.node_id = status.node_id;
  sample.session_id = status.session_id;
  sample.interlock_ready = status.interlock_ready;
  sample.input_bits = status.input_bits;
  sample.fault_code = status.fault_code;
  store.publish_can(sample);
  return CanIngestResult::Updated;
}

}  // namespace rcr::multibus

