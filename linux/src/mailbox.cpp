// Orange Pi/Linux Runtime 实现；不得加入 MCU HAL、FreeRTOS 或 ESP-IDF 依赖。
#include "rcr/mailbox.hpp"

namespace rcr {

// 命令槽位的读写必须在同一把锁内完成，保证消费者不会读到被并发覆盖的半份命令。
void CommandMailbox::publish(const OutputCommand& command) {
  std::lock_guard lock(mutex_);
  if (slot_.has_value()) {
    drop_count_.fetch_add(1, std::memory_order_relaxed);
  }
  slot_ = command;
  publish_count_.fetch_add(1, std::memory_order_relaxed);
}

std::optional<OutputCommand> CommandMailbox::try_consume() {
  std::lock_guard lock(mutex_);
  if (!slot_.has_value()) {
    return std::nullopt;
  }
  OutputCommand cmd = *slot_;
  slot_.reset();
  consume_count_.fetch_add(1, std::memory_order_relaxed);
  return cmd;
}

std::optional<OutputCommand> CommandMailbox::peek() const {
  std::lock_guard lock(mutex_);
  return slot_;
}

void CommandMailbox::clear() {
  std::lock_guard lock(mutex_);
  slot_.reset();
}

bool CommandMailbox::has_pending() const {
  std::lock_guard lock(mutex_);
  return slot_.has_value();
}

std::uint64_t CommandMailbox::publish_count() const noexcept {
  return publish_count_.load(std::memory_order_relaxed);
}

std::uint64_t CommandMailbox::consume_count() const noexcept {
  return consume_count_.load(std::memory_order_relaxed);
}

std::uint64_t CommandMailbox::drop_count() const noexcept {
  return drop_count_.load(std::memory_order_relaxed);
}

}  // namespace rcr
