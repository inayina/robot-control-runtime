#pragma once

// 术语与常量：Modbus TCP 实验的共用地基。
// 读代码建议顺序：types → codec → framing → register_map → client/server → apps。
//
// 直觉：Modbus 是“主站问、从站答”的寄存器读写合同。TCP 承载下，一帧应用数据叫 ADU
//= MBAP（7 字节定界头）+ PDU（function + data）。本文件只钉死字节合同常量与错误码，
// 不碰 socket / 组帧。

#include <cstdint>
#include <string>
#include <vector>

namespace rcr::mbus {

// Protocol ID 恒为 0 表示“这是 Modbus”；其它值视为非法。
inline constexpr std::uint16_t kProtocolId = 0;
inline constexpr std::size_t kMbapSize = 7;
// ADU 上限：MBAP(7) + PDU(最多 253) = 260（Messaging Guide）。超过则拒帧，避免坏 length 吃内存。
inline constexpr std::size_t kMaxAduSize = 260;
inline constexpr std::size_t kMaxPduSize = 253;

// 本实验只实现 Holding Register 三笔：读 / 写单 / 写多。Coil 等未做。
inline constexpr std::uint8_t kFcReadHolding = 0x03;
inline constexpr std::uint8_t kFcWriteSingle = 0x06;
inline constexpr std::uint8_t kFcWriteMultiple = 0x10;

// Exception code：从站拒绝请求时放在“异常 PDU”第二字节。
inline constexpr std::uint8_t kExIllegalFunction = 0x01;
inline constexpr std::uint8_t kExIllegalDataAddress = 0x02;
inline constexpr std::uint8_t kExIllegalDataValue = 0x03;

// 统一错误面：协议坏流 / I/O / 超时 / 从站 exception 分开，便于测试与面试口述。
enum class Error {
  Ok = 0,
  NeedMore,              // framer：字节不够一帧，继续 recv（不是致命错误）
  InvalidProtocolId,     // ProtoID != 0
  InvalidLength,         // MBAP.Length 不合理或 ADU 超限
  Truncated,             // 调用方给的缓冲比合同短
  BufferOverflow,
  UnsupportedFunction,
  IllegalAddress,        // 地址越出 Holding map（映射层）
  IllegalValue,          // quantity / byte_count 等越界
  ExceptionResponse,     // 收到 function|0x80 的异常 PDU（从站明确拒绝）
  TransactionMismatch,   // 响应 TransID ≠ 请求（outstanding>1 或错流时）
  UnexpectedPdu,
  Io,
  Timeout,
  Closed,
  Busy,
};

inline const char* to_string(Error e) {
  switch (e) {
    case Error::Ok: return "ok";
    case Error::NeedMore: return "need_more";
    case Error::InvalidProtocolId: return "invalid_protocol_id";
    case Error::InvalidLength: return "invalid_length";
    case Error::Truncated: return "truncated";
    case Error::BufferOverflow: return "buffer_overflow";
    case Error::UnsupportedFunction: return "unsupported_function";
    case Error::IllegalAddress: return "illegal_address";
    case Error::IllegalValue: return "illegal_value";
    case Error::ExceptionResponse: return "exception_response";
    case Error::TransactionMismatch: return "transaction_mismatch";
    case Error::UnexpectedPdu: return "unexpected_pdu";
    case Error::Io: return "io";
    case Error::Timeout: return "timeout";
    case Error::Closed: return "closed";
    case Error::Busy: return "busy";
  }
  return "unknown";
}

// MBAP 七字节（全部多字节字段为大端）：
//   [0..1] TransID  [2..3] ProtoID=0  [4..5] Length  [6] UnitID
// Length = UnitID(1) + PDU 的字节数；ADU 总长 = 6 + Length（前 6 字节 + Length 所指内容）。
struct Mbap {
  std::uint16_t transaction_id = 0;
  std::uint16_t protocol_id = 0;
  std::uint16_t length = 0;  // Unit + PDU
  std::uint8_t unit_id = 0;
};

struct Adu {
  Mbap mbap{};
  std::vector<std::uint8_t> pdu;  // function + data；异常时 function 带 0x80 位
};

template <typename T>
struct Result {
  Error error = Error::Ok;
  T value{};
  std::string message;

  explicit operator bool() const { return error == Error::Ok; }
};

}  // namespace rcr::mbus
