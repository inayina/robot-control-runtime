#pragma once

// 同步 Modbus TCP 客户端（教学）。
// 约束 outstanding=1：发完一请求必须等对应 TransID 响应再发下一笔。
// 原因：简化匹配与半包缓冲；并发流水线要队列/乱序窗口，本实验刻意不做。
// 超时 / 断线：poll 实现 connect&response timeout；Closed/Io 时指数退避重连并重发该事务。

#include "rcr_mbus/codec.hpp"
#include "rcr_mbus/framing.hpp"
#include "rcr_mbus/types.hpp"

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rcr::mbus {

struct ClientConfig {
  std::string host = "127.0.0.1";
  std::uint16_t port = 1502;  // 学习端口；正式 502 常需特权
  std::uint8_t unit_id = 1;
  std::chrono::milliseconds connect_timeout{1000};
  std::chrono::milliseconds response_timeout{1000};
  // 断线重连：delay = min(base * 2^n, max)，最多 attempts 次。
  std::chrono::milliseconds reconnect_base{50};
  std::chrono::milliseconds reconnect_max{1000};
  int reconnect_attempts = 5;
};

class Client {
 public:
  explicit Client(ClientConfig cfg = {});
  ~Client();

  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  bool connected() const { return fd_ >= 0; }
  Result<bool> connect();
  void close();

  // 高层 API：内部 encode → transact → 识别 exception 位 → decode 响应。
  Result<std::vector<std::uint16_t>> read_holding(std::uint16_t address, std::uint16_t quantity);
  Result<WriteSingleResponse> write_single(std::uint16_t address, std::uint16_t value);
  Result<WriteMultipleResponse> write_multiple(std::uint16_t address,
                                               std::span<const std::uint16_t> values);

  // 原始事务：调用方已备好 PDU；自动分配 TransID（跳过 0 以免与“未初始化”混淆）。
  Result<Adu> transact_raw(std::vector<std::uint8_t> pdu);
  // 测试钩子：强制指定 TransID（用于注入 mismatch 场景）。
  Result<Adu> transact_raw_expect_tid(std::vector<std::uint8_t> pdu, std::uint16_t tid);

 private:
  Result<bool> ensure_connected();
  Result<bool> reconnect_with_backoff();
  Result<Adu> transact_once(std::vector<std::uint8_t> pdu, std::uint16_t tid);
  Result<bool> send_all(std::span<const std::uint8_t> bytes);
  Result<std::vector<std::uint8_t>> recv_adu();

  ClientConfig cfg_;
  int fd_ = -1;
  std::uint16_t next_tid_ = 1;
  StreamFramer framer_;  // 跨 recv 保留半包；close/重连时必须 clear
};

}  // namespace rcr::mbus
