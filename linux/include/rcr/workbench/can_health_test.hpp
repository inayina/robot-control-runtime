#pragma once

// Workbench 应用层的 CAN 通信健康测试。它只观察 Runtime 已发布的快照，不打开
// SocketCAN、不消费 CAN 帧，也不拥有 daemon/transport 生命周期。

#include "rcr/result.hpp"
#include "rcr/workbench/application_model.hpp"
#include "rcr/workbench/test_runner.hpp"

#include <chrono>
#include <functional>
#include <string>

namespace rcr::workbench {

class RuntimeApplicationAdapter;

struct CanHealthCriteria {
  EvidenceClass expected_evidence{EvidenceClass::Vcan};
  std::chrono::milliseconds observation_window{500};
  std::chrono::milliseconds sample_interval{20};
  std::chrono::milliseconds test_timeout{1000};
  std::uint64_t min_heartbeat_delta{3};
  std::chrono::milliseconds max_heartbeat_age{200};
  std::uint64_t max_decode_reject_delta{0};
  std::uint64_t max_queue_reject_delta{0};
  std::uint64_t max_input_queue_drop_delta{0};
};

/**
 * 对一个已经运行的 Runtime 做固定时间窗通信健康判定。
 *
 * SnapshotProvider 和 Wait 是测试接缝：生产路径从 RuntimeApplicationAdapter 取
 * snapshot 并普通 sleep；单元测试可注入确定性时间推进。run() 同步执行，调用方
 * 决定线程；取消和总 deadline 仍由 TestRunner 管理。
 */
class CanCommunicationHealthTest {
public:
  using SnapshotProvider = std::function<RuntimeTelemetrySnapshot()>;
  using Wait = std::function<Result<void>(std::chrono::nanoseconds)>;

  explicit CanCommunicationHealthTest(RuntimeApplicationAdapter &adapter);
  CanCommunicationHealthTest(SnapshotProvider snapshot_provider, Wait wait);

  [[nodiscard]] TestResult run(TestRunner &runner, std::string run_id,
                               CanHealthCriteria criteria = {});

private:
  SnapshotProvider snapshot_provider_;
  Wait wait_;
};

} // namespace rcr::workbench
