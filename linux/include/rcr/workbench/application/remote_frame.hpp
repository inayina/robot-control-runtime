#pragma once

// Remote Workbench 控制面帧格式（v1）：只做长度有界的二进制 framing，不依赖 Qt、
// Runtime 私有结构或真实 socket。TCP 是字节流，一次 read 不等于一帧。

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace rcr::workbench {

inline constexpr std::uint32_t kRemoteFrameMagic = 0x42524352u; // 'R''C''R''B' LE
inline constexpr std::uint8_t kRemoteProtocolVersion = 1;
inline constexpr std::size_t kRemoteFrameHeaderSize = 12;
inline constexpr std::size_t kRemoteFrameCrcSize = 2;
inline constexpr std::size_t kRemoteMaxPayloadSize = 512;
inline constexpr std::size_t kRemoteMaxFrameSize =
    kRemoteFrameHeaderSize + kRemoteMaxPayloadSize + kRemoteFrameCrcSize;
// RX 缓冲至少能装两帧，避免粘包时被迫丢弃仍合法的第二帧前缀。
inline constexpr std::size_t kRemoteRxBufferCapacity = kRemoteMaxFrameSize * 2;

enum class RemoteMessageType : std::uint8_t {
  Hello = 1,
  HelloAck = 2,
  Heartbeat = 3,
  HeartbeatAck = 4,
  GetStatus = 5,
  Status = 6,
  Error = 7,
};

[[nodiscard]] constexpr std::string_view
to_string(RemoteMessageType type) noexcept {
  switch (type) {
  case RemoteMessageType::Hello:
    return "HELLO";
  case RemoteMessageType::HelloAck:
    return "HELLO_ACK";
  case RemoteMessageType::Heartbeat:
    return "HEARTBEAT";
  case RemoteMessageType::HeartbeatAck:
    return "HEARTBEAT_ACK";
  case RemoteMessageType::GetStatus:
    return "GET_STATUS";
  case RemoteMessageType::Status:
    return "STATUS";
  case RemoteMessageType::Error:
    return "ERROR";
  }
  return "UNKNOWN";
}

enum class RemoteFrameParseStatus : std::uint8_t {
  NeedMore = 0,
  Ok,
  InvalidMagic,
  UnsupportedVersion,
  OversizePayload,
  BadCrc,
  BufferOverflow,
};

[[nodiscard]] constexpr std::string_view
to_string(RemoteFrameParseStatus status) noexcept {
  switch (status) {
  case RemoteFrameParseStatus::NeedMore:
    return "NEED_MORE";
  case RemoteFrameParseStatus::Ok:
    return "OK";
  case RemoteFrameParseStatus::InvalidMagic:
    return "INVALID_MAGIC";
  case RemoteFrameParseStatus::UnsupportedVersion:
    return "UNSUPPORTED_VERSION";
  case RemoteFrameParseStatus::OversizePayload:
    return "OVERSIZE_PAYLOAD";
  case RemoteFrameParseStatus::BadCrc:
    return "BAD_CRC";
  case RemoteFrameParseStatus::BufferOverflow:
    return "BUFFER_OVERFLOW";
  }
  return "UNKNOWN";
}

struct RemoteFrame {
  std::uint8_t version{kRemoteProtocolVersion};
  RemoteMessageType type{RemoteMessageType::Error};
  std::uint8_t flags{0};
  std::uint16_t sequence{0};
  std::vector<std::uint8_t> payload{};
};

[[nodiscard]] std::uint16_t remote_crc16(std::span<const std::uint8_t> data) noexcept;

[[nodiscard]] bool encode_remote_frame(const RemoteFrame& frame,
                                       std::vector<std::uint8_t>& out);

// 流式解析器：调用方持续 append 字节，再 try_pop 完整帧。失败时消耗导致错误的
// 前缀（或清空缓冲），避免永久卡在坏 magic 上。
class RemoteFrameParser {
public:
  [[nodiscard]] RemoteFrameParseStatus append(std::span<const std::uint8_t> bytes);
  [[nodiscard]] RemoteFrameParseStatus try_pop(RemoteFrame& frame);
  void clear() noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return buffer_.size(); }

private:
  std::vector<std::uint8_t> buffer_{};
};

} // namespace rcr::workbench
