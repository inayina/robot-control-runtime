#include "rcr/can_v1.hpp"
#include "rcr/workbench/application/cell_ready_mapper.hpp"
#include "test_support.hpp"

namespace {

using rcr::workbench::CellReadyDo0Action;
using rcr::workbench::CellReadyMapper;
using rcr::workbench::evaluate_cell_ready;
using rcr::workbench::RuntimeFaultCode;
using rcr::workbench::RuntimeModeCode;
using rcr::workbench::RuntimeTelemetrySnapshot;

RuntimeTelemetrySnapshot reached_active_online() {
  RuntimeTelemetrySnapshot snap{};
  snap.runtime.mode = RuntimeModeCode::Active;
  snap.runtime.fault = RuntimeFaultCode::None;
  snap.device.online = true;
  snap.device.input_bits = rcr::can_v1::kInputBitPositionReached;
  snap.device.device_fault_code = 0;
  return snap;
}

} // namespace

RCR_TEST(CellReadyRequiresReachedActiveAndOnline) {
  auto snap = reached_active_online();
  auto decision = evaluate_cell_ready(snap);
  RCR_EXPECT(decision.position_reached);
  RCR_EXPECT(decision.cell_ready);

  snap.device.input_bits = 0;
  decision = evaluate_cell_ready(snap);
  RCR_EXPECT(!decision.position_reached);
  RCR_EXPECT(!decision.cell_ready);

  snap = reached_active_online();
  snap.runtime.mode = RuntimeModeCode::Hold;
  RCR_EXPECT(!evaluate_cell_ready(snap).cell_ready);

  snap = reached_active_online();
  snap.runtime.mode = RuntimeModeCode::Fault;
  RCR_EXPECT(!evaluate_cell_ready(snap).cell_ready);

  snap = reached_active_online();
  snap.device.online = false;
  RCR_EXPECT(!evaluate_cell_ready(snap).cell_ready);

  snap = reached_active_online();
  snap.device.device_fault_code = 3;
  RCR_EXPECT(!evaluate_cell_ready(snap).cell_ready);

  snap = reached_active_online();
  snap.runtime.fault = RuntimeFaultCode::CommLoss;
  RCR_EXPECT(!evaluate_cell_ready(snap).cell_ready);
}

RCR_TEST(MapperWritesDo0OnlyOnCellReadyEdge) {
  CellReadyMapper mapper;
  const auto ready = evaluate_cell_ready(reached_active_online());
  auto not_ready = ready;
  not_ready.cell_ready = false;
  not_ready.position_reached = false;

  RCR_EXPECT(mapper.observe(not_ready, true) == CellReadyDo0Action::None);
  RCR_EXPECT(mapper.observe(ready, true) == CellReadyDo0Action::RequestOn);
  RCR_EXPECT(mapper.observe(ready, true) == CellReadyDo0Action::None);
  RCR_EXPECT(mapper.observe(not_ready, true) == CellReadyDo0Action::RequestOff);
}

RCR_TEST(MapperDoesNotReplayAfterModbusLoss) {
  CellReadyMapper mapper;
  const auto ready = evaluate_cell_ready(reached_active_online());
  auto not_ready = ready;
  not_ready.cell_ready = false;

  RCR_EXPECT(mapper.observe(not_ready, true) == CellReadyDo0Action::None);
  RCR_EXPECT(mapper.observe(ready, true) == CellReadyDo0Action::RequestOn);
  mapper.note_modbus_offline();
  RCR_EXPECT(mapper.observe(ready, false) == CellReadyDo0Action::None);
  // Probe 后仍到位：对齐当前值，不自动重放历史 ON。
  RCR_EXPECT(mapper.observe(ready, true) == CellReadyDo0Action::None);
  RCR_EXPECT(mapper.observe(not_ready, true) == CellReadyDo0Action::RequestOff);
  RCR_EXPECT(mapper.observe(ready, true) == CellReadyDo0Action::RequestOn);
}

RCR_TEST_MAIN()
