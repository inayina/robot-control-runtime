#pragma once

// Qt/CLI 可共同使用的进程内应用接缝。公开头不包含 Runtime 类型定义或任何 Qt
// 类型；只在 composition 边界前置声明 RuntimeDaemon 以注入 non-owning 引用。

#include "rcr/workbench/application/application_model.hpp"

#include <string>

namespace rcr {
class RuntimeDaemon;
}

namespace rcr::workbench {

struct RuntimeApplicationAdapterConfig {
  // 证据等级由启动者显式提供，禁止根据 interface_name 猜测真实硬件状态。
  EvidenceClass evidence{EvidenceClass::Unspecified};
  std::string backend_label{"SOCKETCAN"};
};

/**
 * RuntimeDaemon 的非 owning 应用层视图。
 *
 * Adapter 不启动/停止 daemon，不拥有 CAN fd、watchdog 或 fault
 * 状态，也不创建线程。 调用者决定快照节奏；未来 Qt 应在非 realtime 的
 * worker/application 上下文调用，UI 线程只消费返回值。
 */
class RuntimeApplicationAdapter {
public:
  explicit RuntimeApplicationAdapter(
      RuntimeDaemon &daemon, RuntimeApplicationAdapterConfig config = {});

  [[nodiscard]] RuntimeTelemetrySnapshot snapshot() const;
  [[nodiscard]] EvidenceClass evidence_class() const noexcept {
    return config_.evidence;
  }

  [[nodiscard]] CommandReply activate();
  [[nodiscard]] CommandReply deactivate();
  [[nodiscard]] CommandReply clear_fault();
  [[nodiscard]] CommandReply
  submit_digital_output(const DigitalOutputRequest &request);

private:
  RuntimeDaemon &daemon_;
  RuntimeApplicationAdapterConfig config_;
};

} // namespace rcr::workbench
