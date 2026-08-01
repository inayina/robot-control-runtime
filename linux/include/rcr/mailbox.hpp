#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。

#include "rcr/result.hpp"
#include "rcr/types.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>

namespace rcr {

/**
 * Runtime I/O 循环使用的普通输出 latest-wins 邮箱。
 *
 * 多个生产者可以并发发布，I/O线程通常只取最新目标。未消费旧目标会被覆盖并
 * 计入 drop_count，避免追赶已经失效的演示输出目标。输入边沿和故障事件
 * 不得使用本邮箱；它们需要带序号的独立事件路径，队列溢出必须升级为故障。
 * mutex 保护命令对象的一致快照；计数器使用 relaxed 原子，因为它们只用于诊断，
 * 不承担生产者与消费者之间的同步职责。
 */
class CommandMailbox {
 public:
  CommandMailbox() = default;

  CommandMailbox(const CommandMailbox&) = delete;
  CommandMailbox& operator=(const CommandMailbox&) = delete;

  /// 发布命令；接口不阻塞等待空槽，存在未读命令时直接覆盖。
  void publish(const OutputCommand& command);

  /// 取出最新未读命令并清空槽位。
  [[nodiscard]] std::optional<OutputCommand> try_consume();

  /// 获取一致快照但不消费，主要用于诊断读取。
  [[nodiscard]] std::optional<OutputCommand> peek() const;

  void clear();

  [[nodiscard]] bool has_pending() const;
  [[nodiscard]] std::uint64_t publish_count() const noexcept;
  [[nodiscard]] std::uint64_t consume_count() const noexcept;
  [[nodiscard]] std::uint64_t drop_count() const noexcept;

 private:
  mutable std::mutex mutex_;
  std::optional<OutputCommand> slot_{};
  std::atomic<std::uint64_t> publish_count_{0};
  std::atomic<std::uint64_t> consume_count_{0};
  std::atomic<std::uint64_t> drop_count_{0};
};

}  // namespace rcr
