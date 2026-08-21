#include "rcr/workbench/application/cell_ready_mapper.hpp"

#include "rcr/can_v1.hpp"

namespace rcr::workbench {

CellReadyDecision
evaluate_cell_ready(const RuntimeTelemetrySnapshot &snapshot) noexcept {
  CellReadyDecision out{};
  out.position_reached =
      (snapshot.device.input_bits & can_v1::kInputBitPositionReached) != 0;
  // CellReady 策略看 Runtime 已解码快照，不看 PWM/lease/output_mirror。
  out.cell_ready = snapshot.device.online &&
                   snapshot.runtime.mode == RuntimeModeCode::Active &&
                   out.position_reached &&
                   snapshot.device.device_fault_code == 0 &&
                   snapshot.runtime.fault == RuntimeFaultCode::None;
  return out;
}

void CellReadyMapper::note_modbus_offline() noexcept { armed_ = false; }

void CellReadyMapper::synchronize_after_probe(
    const CellReadyDecision &decision) noexcept {
  armed_ = true;
  last_ready_ = decision.cell_ready;
}

CellReadyDo0Action
CellReadyMapper::observe(const CellReadyDecision &decision,
                         bool modbus_online) noexcept {
  if (!modbus_online) {
    armed_ = false;
    last_ready_ = decision.cell_ready;
    return CellReadyDo0Action::None;
  }
  if (!armed_) {
    // Probe 后先对齐当前值，不把掉线前的 DO 当新命令重放。
    synchronize_after_probe(decision);
    return CellReadyDo0Action::None;
  }
  if (decision.cell_ready == last_ready_) {
    return CellReadyDo0Action::None;
  }
  last_ready_ = decision.cell_ready;
  return decision.cell_ready ? CellReadyDo0Action::RequestOn
                             : CellReadyDo0Action::RequestOff;
}

} // namespace rcr::workbench
