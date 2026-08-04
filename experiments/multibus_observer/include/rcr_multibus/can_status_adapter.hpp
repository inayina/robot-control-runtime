#pragma once

// 协议→数据窄适配：只负责 CAN V1 NodeStatus 到强类型观测的转换。
// fd readiness 与线程属于 app/I/O 层，快照存储属于 observation 数据层。

#include "rcr/types.hpp"
#include "rcr_multibus/observation.hpp"

#include <cstdint>

namespace rcr::multibus {

enum class CanIngestResult : std::uint8_t {
  Updated = 0,
  Ignored = 1,
  Rejected = 2,
};

/**
 * 只接收目标节点的 CAN V1 NodeStatus。Heartbeat、OutputStatus、下行回环及其他节点
 * 都是合法但与本观测卡无关的数据，因此返回 Ignored；坏线帧返回 Rejected。
 */
[[nodiscard]] CanIngestResult ingest_can_frame(ObservationStore& store,
                                               const CanFrame& frame,
                                               std::uint8_t expected_node_id,
                                               std::int64_t sampled_ns);

}  // namespace rcr::multibus

