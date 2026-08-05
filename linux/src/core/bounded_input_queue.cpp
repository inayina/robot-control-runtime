// Runtime Core：无策略的有界输入事件队列；不解释节点语义或决定状态迁移。
#include "rcr/runtime_events.hpp"

namespace rcr {

BoundedInputQueue::BoundedInputQueue(std::size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity) {
  // 容量在构造时固定分配，周期路径不再扩容，避免监督线程隐式分配。
  storage_.resize(capacity_);
}

std::size_t BoundedInputQueue::size() const {
  std::lock_guard lock(mutex_);
  return size_;
}

bool BoundedInputQueue::empty() const {
  std::lock_guard lock(mutex_);
  return size_ == 0;
}

std::uint64_t BoundedInputQueue::push_count() const noexcept {
  return push_count_.load(std::memory_order_relaxed);
}

std::uint64_t BoundedInputQueue::drop_count() const noexcept {
  return drop_count_.load(std::memory_order_relaxed);
}

std::uint64_t BoundedInputQueue::overflow_count() const noexcept {
  return overflow_count_.load(std::memory_order_relaxed);
}

bool BoundedInputQueue::overflow_latched() const noexcept {
  return overflow_latched_.load(std::memory_order_acquire);
}

bool BoundedInputQueue::try_push(const RuntimeInputEvent &event) {
  std::lock_guard lock(mutex_);
  if (size_ >= capacity_) {
    // 满队列时不覆盖任何已有事件：故障边沿必须可见，宁可锁存 overflow。
    drop_count_.fetch_add(1, std::memory_order_relaxed);
    overflow_count_.fetch_add(1, std::memory_order_relaxed);
    overflow_latched_.store(true, std::memory_order_release);
    return false;
  }
  const std::size_t index = (head_ + size_) % capacity_;
  storage_[index] = event;
  ++size_;
  push_count_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

std::optional<RuntimeInputEvent> BoundedInputQueue::try_pop() {
  std::lock_guard lock(mutex_);
  if (size_ == 0) {
    return std::nullopt;
  }
  RuntimeInputEvent event = storage_[head_];
  head_ = (head_ + 1) % capacity_;
  --size_;
  return event;
}

void BoundedInputQueue::clear_overflow_latch() noexcept {
  overflow_latched_.store(false, std::memory_order_release);
}

} // namespace rcr
