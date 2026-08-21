#pragma once

// CellReady 是 Workbench 应用层对单元外围 I/O 的决策，不是 CAN 字段，也不进 Runtime
// Core / rcrd。CAN 只提供机器人节点 input_bits；本文件把它映射成 CellReady。
// 物理演示由 rcr_cell_app 边沿写 DO0；本机 vcan Qt 仍可在同进程 Controller 里 tick。
// decoder 不得直接写线圈。

#include "rcr/workbench/application/application_model.hpp"

namespace rcr::workbench {

struct CellReadyDecision {
  bool position_reached{false};
  bool cell_ready{false};
};

enum class CellReadyDo0Action : std::uint8_t {
  None = 0,
  RequestOn,
  RequestOff,
};

[[nodiscard]] CellReadyDecision
evaluate_cell_ready(const RuntimeTelemetrySnapshot &snapshot) noexcept;

/**
 * 只在 CellReady 边沿请求 DO0，避免把历史 ON 在 RS485 恢复后自动重放。
 * Modbus 掉线后必须重新 Probe；下一次边沿之前不写线圈。
 */
class CellReadyMapper {
public:
  void note_modbus_offline() noexcept;
  // 只读 Probe 成功后把当前 CellReady 作为新的边沿基准。这里绝不产生 DO0 action；
  // 否则 RS-485 恢复会把掉线前的历史 ON 误当成新命令重放。
  void synchronize_after_probe(const CellReadyDecision &decision) noexcept;
  [[nodiscard]] CellReadyDo0Action
  observe(const CellReadyDecision &decision, bool modbus_online) noexcept;

private:
  bool armed_{false};
  bool last_ready_{false};
};

} // namespace rcr::workbench
