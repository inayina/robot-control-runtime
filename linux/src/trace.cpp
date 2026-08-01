// Orange Pi/Linux Runtime 实现；不得加入 MCU HAL、FreeRTOS 或 ESP-IDF 依赖。
#include "rcr/trace.hpp"

#include <algorithm>

namespace rcr {

TraceBuffer::TraceBuffer(std::size_t capacity) : capacity_(capacity), storage_(capacity) {}

void TraceBuffer::record(const TraceEvent& event) noexcept {
  if (capacity_ == 0) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  // 周期监督路径不能等待慢诊断读者。try_lock 失败时牺牲 trace 完整性，而不是放大
  // 调度延迟；dropped 让这种牺牲可观测。
  std::unique_lock lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  // 先写当前槽，再以模运算推进 next。写满后 next 指向最旧项，也就是下一次覆盖位置。
  storage_[next_] = event;
  next_ = (next_ + 1) % capacity_;
  size_ = std::min(size_ + 1, capacity_);
}

std::vector<TraceEvent> TraceBuffer::snapshot() const {
  // snapshot 持锁期间 record 会选择丢弃而不是等待，所以调用频率和复制量会直接影响
  // dropped。该接口用于低频诊断导出，不应每个周期调用。
  std::lock_guard lock(mutex_);
  std::vector<TraceEvent> result;
  result.reserve(size_);
  // 未写满时有效数据从 0 开始；写满后 next_ 已回绕并指向逻辑最旧事件。
  const std::size_t oldest = size_ == capacity_ ? next_ : 0;
  for (std::size_t offset = 0; offset < size_; ++offset) {
    result.push_back(storage_[(oldest + offset) % capacity_]);
  }
  return result;
}

std::size_t TraceBuffer::capacity() const noexcept { return capacity_; }

std::uint64_t TraceBuffer::dropped() const noexcept {
  return dropped_.load(std::memory_order_relaxed);
}

}  // namespace rcr
