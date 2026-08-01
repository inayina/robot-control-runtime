// Orange Pi/Linux Runtime 实现；不得加入 MCU HAL、FreeRTOS 或 ESP-IDF 依赖。
// 编解码严格按 protocol/can_v1/README.md；不 memcpy 结构体内存布局。
#include "rcr/can_v1.hpp"

#include <cstring>

namespace rcr::can_v1 {
namespace {

constexpr std::uint32_t kCanEffFlag = 0x80000000u;
constexpr std::uint32_t kCanRtrFlag = 0x40000000u;
constexpr std::uint32_t kCanSffMask = 0x7FFu;

Error reject(const char* message) {
  // decode 收到的是外部/总线输入：格式不符合合同属于 Rejected，而不是调用者参数错误。
  return Error{Errc::Rejected, message};
}

Error invalid(const char* message) {
  // encode 的 DTO 来自本进程：无法编码说明本地调用参数违反合同，用 InvalidArgument。
  return Error{Errc::InvalidArgument, message};
}

void write_u16_be(std::uint8_t* dest, std::uint16_t value) {
  // 显式移位得到网络序，结果与主机端大小端和结构体对齐无关。
  dest[0] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
  dest[1] = static_cast<std::uint8_t>(value & 0xFFu);
}

std::uint16_t read_u16_be(const std::uint8_t* src) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(src[0]) << 8) |
                                    static_cast<std::uint16_t>(src[1]));
}

Result<void> check_frame_layer(const CanFrame& frame, Function expected) {
  // 检查顺序从链路层外形到消息类型：flags → DLC → node/function。只有全部通过后，
  // 各消息 decoder 才能安全、无条件地读取 data[0..7]。
  // SocketCAN 把 EFF/RTR 放在 can_id 高位；标准 ID 只用低 11 bit。
  if ((frame.can_id & kCanEffFlag) != 0u) {
    return reject("extended CAN frame rejected");
  }
  if ((frame.can_id & kCanRtrFlag) != 0u) {
    return reject("RTR frame rejected");
  }
  if (frame.len != 8) {
    return reject("CAN V1 requires DLC=8");
  }
  const std::uint32_t raw11 = frame.can_id & kCanSffMask;
  const std::uint8_t node = node_id_from_can_id(raw11);
  const std::uint8_t function = function_from_can_id(raw11);
  if (node == 0 || node > kMaxNodeId) {
    return reject("invalid node_id");
  }
  if (function != static_cast<std::uint8_t>(expected)) {
    return reject("CAN ID function mismatch");
  }
  return Result<void>::success();
}

Result<void> check_version(const CanFrame& frame) {
  // 版本不匹配不尝试“尽量解析”：同一字节在新版本可能已有不同语义，fail closed 更可诊断。
  if (frame.data[0] != kProtocolVersion) {
    return reject("unsupported protocol_version");
  }
  return Result<void>::success();
}

Result<void> validate_node_id(std::uint8_t node_id) {
  if (node_id == 0 || node_id > kMaxNodeId) {
    return invalid("node_id must be 1..31");
  }
  return Result<void>::success();
}

CanFrame make_frame(Function function, std::uint8_t node_id) {
  // 值初始化和 memset 共同保证 reserved 字节为 0。encode 只覆盖合同定义字段，避免栈上
  // 未初始化字节进入总线并造成不同编译器/构建类型下的随机协议行为。
  CanFrame frame{};
  frame.can_id = make_can_id(function, node_id);
  frame.len = 8;
  std::memset(frame.data, 0, sizeof(frame.data));
  frame.data[0] = kProtocolVersion;
  return frame;
}

}  // namespace

Result<std::uint8_t> validity_10ms_from_deadline(std::int64_t now_ns,
                                                std::int64_t deadline_ns) {
  // 绝对 monotonic deadline 只在发送进程内有意义；线上只传剩余有效期。
  const std::int64_t remaining = deadline_ns - now_ns;
  if (remaining < kValidityUnitNs) {
    return invalid("deadline remaining below 10 ms");
  }
  // 向上取整到 10 ms 单位，再夹到合同上限，超出则拒绝发送。
  const std::int64_t units =
      (remaining + kValidityUnitNs - 1) / kValidityUnitNs;
  if (units > kMaxValidity10ms) {
    return invalid("deadline remaining above 2500 ms");
  }
  return static_cast<std::uint8_t>(units);
}

Result<CanFrame> encode_heartbeat(const WireHeartbeat& msg) {
  // encode 先验证所有语义字段，后构造 frame；失败不会返回“部分有效”的 can_id/payload。
  if (auto rc = validate_node_id(msg.node_id); !rc) {
    return rc.error();
  }
  if (msg.boot_id == 0) {
    return invalid("boot_id must be non-zero");
  }
  if (msg.session_id == 0) {
    return invalid("session_id must be non-zero");
  }

  CanFrame frame = make_frame(Function::Heartbeat, msg.node_id);
  // flags 保持 0；逐字段写入，避免结构体内存布局进入总线。
  write_u16_be(&frame.data[2], msg.boot_id);
  write_u16_be(&frame.data[4], msg.session_id);
  write_u16_be(&frame.data[6], msg.hb_seq);
  return frame;
}

Result<CanFrame> encode_node_status(const WireNodeStatus& msg) {
  if (auto rc = validate_node_id(msg.node_id); !rc) {
    return rc.error();
  }
  if (msg.session_id == 0) {
    return invalid("session_id must be non-zero");
  }

  CanFrame frame = make_frame(Function::Status, msg.node_id);
  frame.data[1] = msg.interlock_ready ? 0x01u : 0x00u;
  write_u16_be(&frame.data[2], msg.session_id);
  write_u16_be(&frame.data[4], msg.input_bits);
  write_u16_be(&frame.data[6], msg.fault_code);
  return frame;
}

Result<CanFrame> encode_output_command(const WireOutputCommand& msg) {
  if (auto rc = validate_node_id(msg.node_id); !rc) {
    return rc.error();
  }
  if (msg.mask == 0) {
    return invalid("mask must be non-zero");
  }
  if (msg.session_id == 0) {
    return invalid("session_id must be non-zero");
  }
  if (msg.sequence == 0) {
    return invalid("sequence must be non-zero");
  }
  if (msg.validity_10ms < kMinValidity10ms ||
      msg.validity_10ms > kMaxValidity10ms) {
    return invalid("validity_10ms must be 1..250");
  }

  CanFrame frame = make_frame(Function::OutputCommand, msg.node_id);
  frame.data[1] = msg.mask;
  write_u16_be(&frame.data[2], msg.session_id);
  write_u16_be(&frame.data[4], msg.sequence);
  frame.data[6] = msg.values;
  frame.data[7] = msg.validity_10ms;
  return frame;
}

Result<CanFrame> encode_output_status(const WireOutputStatus& msg) {
  if (auto rc = validate_node_id(msg.node_id); !rc) {
    return rc.error();
  }
  if (msg.session_id == 0) {
    return invalid("session_id must be non-zero");
  }
  const auto result = static_cast<std::uint8_t>(msg.result);
  if (result > static_cast<std::uint8_t>(OutputResult::NotReady)) {
    return invalid("output result is reserved");
  }

  CanFrame frame = make_frame(Function::OutputStatus, msg.node_id);
  frame.data[1] = result;  // 高 4 bit 保持 0
  write_u16_be(&frame.data[2], msg.session_id);
  write_u16_be(&frame.data[4], msg.sequence);
  frame.data[6] = msg.output_mirror;
  // byte7 reserved = 0
  return frame;
}

Result<WireHeartbeat> decode_heartbeat(const CanFrame& frame) {
  if (auto rc = check_frame_layer(frame, Function::Heartbeat); !rc) {
    return rc.error();
  }
  if (auto rc = check_version(frame); !rc) {
    return rc.error();
  }
  if (frame.data[1] != 0) {
    return reject("heartbeat flags must be zero");
  }

  WireHeartbeat msg{};
  msg.node_id = node_id_from_can_id(frame.can_id & kCanSffMask);
  msg.boot_id = read_u16_be(&frame.data[2]);
  msg.session_id = read_u16_be(&frame.data[4]);
  msg.hb_seq = read_u16_be(&frame.data[6]);
  if (msg.boot_id == 0) {
    return reject("boot_id must be non-zero");
  }
  if (msg.session_id == 0) {
    return reject("session_id must be non-zero");
  }
  return msg;
}

Result<WireNodeStatus> decode_node_status(const CanFrame& frame) {
  if (auto rc = check_frame_layer(frame, Function::Status); !rc) {
    return rc.error();
  }
  if (auto rc = check_version(frame); !rc) {
    return rc.error();
  }
  if ((frame.data[1] & 0xFEu) != 0) {
    return reject("node status reserved flags must be zero");
  }

  WireNodeStatus msg{};
  msg.node_id = node_id_from_can_id(frame.can_id & kCanSffMask);
  msg.interlock_ready = (frame.data[1] & 0x01u) != 0;
  msg.session_id = read_u16_be(&frame.data[2]);
  msg.input_bits = read_u16_be(&frame.data[4]);
  msg.fault_code = read_u16_be(&frame.data[6]);
  if (msg.session_id == 0) {
    return reject("session_id must be non-zero");
  }
  return msg;
}

Result<WireOutputCommand> decode_output_command(const CanFrame& frame) {
  if (auto rc = check_frame_layer(frame, Function::OutputCommand); !rc) {
    return rc.error();
  }
  if (auto rc = check_version(frame); !rc) {
    return rc.error();
  }

  WireOutputCommand msg{};
  msg.node_id = node_id_from_can_id(frame.can_id & kCanSffMask);
  msg.mask = frame.data[1];
  msg.session_id = read_u16_be(&frame.data[2]);
  msg.sequence = read_u16_be(&frame.data[4]);
  msg.values = frame.data[6];
  msg.validity_10ms = frame.data[7];

  // 线级非法直接拒绝；session 是否“当前”、sequence 相对历史是否更新、接收后是否过期
  // 都依赖节点运行状态，必须留给 CanNodeLogic，不能让无状态 codec 隐藏业务历史。
  if (msg.mask == 0) {
    return reject("mask must be non-zero");
  }
  if (msg.session_id == 0) {
    return reject("session_id must be non-zero");
  }
  if (msg.sequence == 0) {
    return reject("sequence must be non-zero");
  }
  if (msg.validity_10ms < kMinValidity10ms ||
      msg.validity_10ms > kMaxValidity10ms) {
    return reject("validity_10ms must be 1..250");
  }
  return msg;
}

Result<WireOutputStatus> decode_output_status(const CanFrame& frame) {
  if (auto rc = check_frame_layer(frame, Function::OutputStatus); !rc) {
    return rc.error();
  }
  if (auto rc = check_version(frame); !rc) {
    return rc.error();
  }
  if ((frame.data[1] & 0xF0u) != 0) {
    return reject("output status result high nibble must be zero");
  }
  if (frame.data[7] != 0) {
    return reject("output status reserved byte must be zero");
  }

  const std::uint8_t result = frame.data[1] & 0x0Fu;
  if (result > static_cast<std::uint8_t>(OutputResult::NotReady)) {
    return reject("output result is reserved");
  }

  WireOutputStatus msg{};
  msg.node_id = node_id_from_can_id(frame.can_id & kCanSffMask);
  msg.result = static_cast<OutputResult>(result);
  msg.session_id = read_u16_be(&frame.data[2]);
  msg.sequence = read_u16_be(&frame.data[4]);
  msg.output_mirror = frame.data[6];
  if (msg.session_id == 0) {
    return reject("session_id must be non-zero");
  }
  return msg;
}

Result<DecodedMessage> decode(const CanFrame& frame) {
  // 通用入口只根据标准 ID 的 function 分派，具体 decoder 仍会重复完整帧层检查。
  // 少量重复换来每个专用 decode_* 都可被独立安全调用，不依赖“上层已经检查过”。
  if ((frame.can_id & kCanEffFlag) != 0u) {
    return reject("extended CAN frame rejected");
  }
  if ((frame.can_id & kCanRtrFlag) != 0u) {
    return reject("RTR frame rejected");
  }
  if (frame.len != 8) {
    return reject("CAN V1 requires DLC=8");
  }

  const std::uint32_t raw11 = frame.can_id & kCanSffMask;
  const std::uint8_t node = node_id_from_can_id(raw11);
  const std::uint8_t function = function_from_can_id(raw11);
  if (node == 0 || node > kMaxNodeId) {
    return reject("invalid node_id");
  }

  DecodedMessage out{};
  switch (function) {
    case static_cast<std::uint8_t>(Function::Heartbeat): {
      auto msg = decode_heartbeat(frame);
      if (!msg) {
        return msg.error();
      }
      out.kind = MessageKind::Heartbeat;
      out.heartbeat = msg.value();
      return out;
    }
    case static_cast<std::uint8_t>(Function::Status): {
      auto msg = decode_node_status(frame);
      if (!msg) {
        return msg.error();
      }
      out.kind = MessageKind::Status;
      out.status = msg.value();
      return out;
    }
    case static_cast<std::uint8_t>(Function::OutputCommand): {
      auto msg = decode_output_command(frame);
      if (!msg) {
        return msg.error();
      }
      out.kind = MessageKind::OutputCommand;
      out.command = msg.value();
      return out;
    }
    case static_cast<std::uint8_t>(Function::OutputStatus): {
      auto msg = decode_output_status(frame);
      if (!msg) {
        return msg.error();
      }
      out.kind = MessageKind::OutputStatus;
      out.output_status = msg.value();
      return out;
    }
    default:
      return reject("unknown CAN function");
  }
}

}  // namespace rcr::can_v1
