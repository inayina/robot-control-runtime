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
  std::unique_lock lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  storage_[next_] = event;
  next_ = (next_ + 1) % capacity_;
  size_ = std::min(size_ + 1, capacity_);
}

std::vector<TraceEvent> TraceBuffer::snapshot() const {
  std::lock_guard lock(mutex_);
  std::vector<TraceEvent> result;
  result.reserve(size_);
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
