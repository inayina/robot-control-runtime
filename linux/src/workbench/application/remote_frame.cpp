#include "rcr/workbench/application/remote_frame.hpp"

namespace rcr::workbench {
namespace {

void append_u16_le(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void append_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

[[nodiscard]] std::uint16_t read_u16_le(const std::uint8_t* p) noexcept {
  return static_cast<std::uint16_t>(p[0]) |
         (static_cast<std::uint16_t>(p[1]) << 8);
}

[[nodiscard]] std::uint32_t read_u32_le(const std::uint8_t* p) noexcept {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

} // namespace

std::uint16_t remote_crc16(std::span<const std::uint8_t> data) noexcept {
  // CRC-16/IBM（与 Modbus RTU 同多项式），便于对照已有 Modbus 笔记；此处只服务
  // Remote frame 完整性，不是现场总线证据。
  std::uint16_t crc = 0xFFFFu;
  for (const std::uint8_t byte : data) {
    crc ^= static_cast<std::uint16_t>(byte);
    for (int bit = 0; bit < 8; ++bit) {
      if ((crc & 0x0001u) != 0) {
        crc = static_cast<std::uint16_t>((crc >> 1) ^ 0xA001u);
      } else {
        crc = static_cast<std::uint16_t>(crc >> 1);
      }
    }
  }
  return crc;
}

bool encode_remote_frame(const RemoteFrame& frame,
                         std::vector<std::uint8_t>& out) {
  if (frame.payload.size() > kRemoteMaxPayloadSize) {
    return false;
  }
  if (frame.version != kRemoteProtocolVersion) {
    return false;
  }

  const std::size_t start = out.size();
  append_u32_le(out, kRemoteFrameMagic);
  out.push_back(frame.version);
  out.push_back(static_cast<std::uint8_t>(frame.type));
  out.push_back(frame.flags);
  out.push_back(0); // reserved：保持头部长 12，避免以后插字段时破坏长度合同
  append_u16_le(out, frame.sequence);
  append_u16_le(out, static_cast<std::uint16_t>(frame.payload.size()));
  out.insert(out.end(), frame.payload.begin(), frame.payload.end());
  const auto crc = remote_crc16(std::span<const std::uint8_t>(
      out.data() + static_cast<std::ptrdiff_t>(start),
      out.size() - start));
  append_u16_le(out, crc);
  return true;
}

RemoteFrameParseStatus
RemoteFrameParser::append(std::span<const std::uint8_t> bytes) {
  if (bytes.empty()) {
    return RemoteFrameParseStatus::NeedMore;
  }
  if (buffer_.size() + bytes.size() > kRemoteRxBufferCapacity) {
    buffer_.clear();
    return RemoteFrameParseStatus::BufferOverflow;
  }
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
  return RemoteFrameParseStatus::NeedMore;
}

RemoteFrameParseStatus RemoteFrameParser::try_pop(RemoteFrame& frame) {
  if (buffer_.size() < kRemoteFrameHeaderSize) {
    return RemoteFrameParseStatus::NeedMore;
  }

  const std::uint8_t* data = buffer_.data();
  const auto magic = read_u32_le(data);
  if (magic != kRemoteFrameMagic) {
    // 丢掉 1 字节后重试，避免一次坏字节永久阻塞后续合法帧。
    buffer_.erase(buffer_.begin());
    return RemoteFrameParseStatus::InvalidMagic;
  }

  const auto version = data[4];
  if (version != kRemoteProtocolVersion) {
    buffer_.erase(buffer_.begin(),
                  buffer_.begin() +
                      static_cast<std::ptrdiff_t>(kRemoteFrameHeaderSize));
    return RemoteFrameParseStatus::UnsupportedVersion;
  }

  const auto payload_len = read_u16_le(data + 10);
  if (payload_len > kRemoteMaxPayloadSize) {
    buffer_.erase(buffer_.begin(),
                  buffer_.begin() +
                      static_cast<std::ptrdiff_t>(kRemoteFrameHeaderSize));
    return RemoteFrameParseStatus::OversizePayload;
  }

  const std::size_t frame_len =
      kRemoteFrameHeaderSize + payload_len + kRemoteFrameCrcSize;
  if (buffer_.size() < frame_len) {
    return RemoteFrameParseStatus::NeedMore;
  }

  const auto expected_crc = remote_crc16(
      std::span<const std::uint8_t>(data, kRemoteFrameHeaderSize + payload_len));
  const auto actual_crc =
      read_u16_le(data + kRemoteFrameHeaderSize + payload_len);
  if (expected_crc != actual_crc) {
    buffer_.erase(buffer_.begin(),
                  buffer_.begin() + static_cast<std::ptrdiff_t>(frame_len));
    return RemoteFrameParseStatus::BadCrc;
  }

  frame.version = version;
  frame.type = static_cast<RemoteMessageType>(data[5]);
  frame.flags = data[6];
  frame.sequence = read_u16_le(data + 8);
  frame.payload.assign(data + kRemoteFrameHeaderSize,
                       data + kRemoteFrameHeaderSize + payload_len);
  buffer_.erase(buffer_.begin(),
                buffer_.begin() + static_cast<std::ptrdiff_t>(frame_len));
  return RemoteFrameParseStatus::Ok;
}

void RemoteFrameParser::clear() noexcept { buffer_.clear(); }

} // namespace rcr::workbench
