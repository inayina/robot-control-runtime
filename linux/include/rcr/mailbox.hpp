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

  /// 发布命令；只在 mutex 获取期间阻塞，不等待消费者腾出空槽。存在未读命令时覆盖，
  /// publish_count 每次增加，drop_count 只在确实覆盖未读值时增加。
  void publish(const OutputCommand& command);

  /// 在同一锁区间复制并清空最新命令；成功消费才增加 consume_count。
  [[nodiscard]] std::optional<OutputCommand> try_consume();

  /// 获取一致快照但不消费，主要用于诊断读取。
  [[nodiscard]] std::optional<OutputCommand> peek() const;

  /// 丢弃当前未读槽位但不增加 drop_count；用于状态退出的主动 fail-closed 清理。
  void clear();

  [[nodiscard]] bool has_pending() const;
  [[nodiscard]] std::uint64_t publish_count() const noexcept;
  [[nodiscard]] std::uint64_t consume_count() const noexcept;
  [[nodiscard]] std::uint64_t drop_count() const noexcept;

 private:
  // mutex 保护整个 optional<OutputCommand>，确保消费者看到来自同一次 publish 的完整字段。
  mutable std::mutex mutex_;
  std::optional<OutputCommand> slot_{};
  // 计数器不用于命令发布同步，只做诊断，因而可在槽位锁外用 relaxed 读取。
  std::atomic<std::uint64_t> publish_count_{0};
  std::atomic<std::uint64_t> consume_count_{0};
  std::atomic<std::uint64_t> drop_count_{0};
};

}  // namespace rcr
