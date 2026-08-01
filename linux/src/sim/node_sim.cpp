// Simulator 层：独立节点业务逻辑，不进入生产 Runtime 控制决策。
#include "rcr/node_sim.hpp"

namespace rcr {
namespace {

constexpr std::uint32_t kCanSffMask = 0x7FFu;

}  // namespace

CanNodeLogic::CanNodeLogic(Config config) : config_(config) {
  // 线协议禁止 boot/session=0。CLI 和正式调用应在构造前拒绝；这里归一到 1 让纯逻辑对象
  // 始终能产生可编码 heartbeat，而不是把无效状态推迟到每次发送。
  boot_id_ = config.boot_id == 0 ? 1 : config.boot_id;
  session_id_ = config.session_id == 0 ? 1 : config.session_id;
}

std::uint16_t CanNodeLogic::next_nonzero_u16(std::uint16_t value) noexcept {
  // 合同禁止 0；65535 之后回到 1。
  return value == 65535 ? 1 : static_cast<std::uint16_t>(value + 1);
}

void CanNodeLogic::set_interlock_ready(bool ready) noexcept {
  config_.interlock_ready = ready;
}

void CanNodeLogic::set_input_bits(std::uint16_t bits) noexcept {
  config_.input_bits = bits;
}

void CanNodeLogic::set_fault_code(std::uint16_t code) noexcept {
  config_.fault_code = code;
}

void CanNodeLogic::soft_restart() {
  // restart 是一个会话边界：输出和序号历史必须一起清除。若只换 session 不清 output，
  // 新会话会在尚未收到新命令时继续保持旧目标，违反“不自动重放”。
  boot_id_ = next_nonzero_u16(boot_id_);
  session_id_ = next_nonzero_u16(session_id_);
  hb_seq_ = 0;
  output_bits_ = 0;
  has_accepted_sequence_ = false;
  last_accepted_sequence_ = 0;
}

can_v1::WireHeartbeat CanNodeLogic::make_heartbeat() {
  can_v1::WireHeartbeat msg{};
  msg.node_id = config_.node_id;
  msg.boot_id = boot_id_;
  msg.session_id = session_id_;
  msg.hb_seq = hb_seq_;
  // 发送后再递增，使线上首帧从当前值开始；允许回绕到 0。
  hb_seq_ = static_cast<std::uint16_t>(hb_seq_ + 1);
  return msg;
}

can_v1::WireNodeStatus CanNodeLogic::make_status() const {
  can_v1::WireNodeStatus msg{};
  msg.node_id = config_.node_id;
  msg.interlock_ready = config_.interlock_ready;
  msg.session_id = session_id_;
  msg.input_bits = config_.input_bits;
  msg.fault_code = config_.fault_code;
  return msg;
}

CanNodeLogic::HandleResult CanNodeLogic::apply_command(
    const can_v1::WireOutputCommand& cmd, std::int64_t receive_ns,
    std::int64_t now_ns) {
  HandleResult result{};
  result.send_status = true;
  result.status.node_id = config_.node_id;
  result.status.session_id = session_id_;
  result.status.sequence = cmd.sequence;
  result.status.output_mirror = output_bits_;

  // 按合同顺序返回第一个拒绝原因。所有拒绝分支都保留当前 output_mirror，且不推进
  // last_accepted_sequence；因此发送方可以用更正后的同一/新序号重新尝试。
  if (cmd.session_id != session_id_) {
    result.status.result = can_v1::OutputResult::SessionMismatch;
    return result;
  }
  if (!config_.interlock_ready) {
    result.status.result = can_v1::OutputResult::NotReady;
    return result;
  }
  if (has_accepted_sequence_ &&
      !can_v1::seq_newer(cmd.sequence, last_accepted_sequence_)) {
    result.status.result = can_v1::OutputResult::StaleSequence;
    return result;
  }

  // deadline 锚定在接收时刻；延迟应用时用 now_ns 判定是否已过期。
  const std::int64_t deadline =
      can_v1::deadline_from_validity_10ms(receive_ns, cmd.validity_10ms);
  if (now_ns >= deadline) {
    result.status.result = can_v1::OutputResult::Expired;
    return result;
  }

  // mask=1 的 bit 才采用 values，其余 bit 保持原状。~cmd.mask 先显式收窄到 u8，避免
  // 整数提升后的高位参与表达式并掩盖 8 路输出合同。
  output_bits_ = static_cast<std::uint8_t>(
      (output_bits_ & static_cast<std::uint8_t>(~cmd.mask)) | (cmd.values & cmd.mask));
  has_accepted_sequence_ = true;
  last_accepted_sequence_ = cmd.sequence;
  result.status.result = can_v1::OutputResult::Applied;
  result.status.output_mirror = output_bits_;
  return result;
}

CanNodeLogic::HandleResult CanNodeLogic::on_frame(const CanFrame& frame,
                                                 std::int64_t now_ns) {
  HandleResult result{};
  const std::uint32_t raw11 = frame.can_id & kCanSffMask;
  const auto function = can_v1::function_from_can_id(raw11);
  const auto node = can_v1::node_id_from_can_id(raw11);

  // CAN 是广播总线，同一 socket 可能看到自己发送的 heartbeat/status 以及其他节点流量。
  // 只处理发往本节点的 OutputCommand；其它 ID 保持静默，避免把旁路 HB 当协议拒绝。
  if (function != static_cast<std::uint8_t>(can_v1::Function::OutputCommand) ||
      node != config_.node_id) {
    return result;
  }

  const auto decoded = can_v1::decode_output_command(frame);
  if (!decoded) {
    // 对无法完整解码的命令不能构造可靠 OutputStatus（sequence/session 可能就是坏字段），
    // 只增加本地拒绝计数。验收端通过非法帧后正常帧仍可处理来验证 fail closed。
    ++protocol_rejects_;
    result.protocol_reject = true;
    return result;
  }
  return apply_command(decoded.value(), now_ns, now_ns);
}

}  // namespace rcr
