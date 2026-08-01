#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。

#include "rcr/types.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace rcr {

enum class TraceKind : std::uint8_t {
  SchedulerTick = 0,
  DeadlineMiss = 1,
  StateTransition = 2,
  OutputCommandPublished = 3,
  WatchdogExpired = 4,
  OutputCommandRejected = 5,
};

struct TraceEvent {
  std::int64_t timestamp_ns{0};
  TraceKind kind{TraceKind::SchedulerTick};
  std::int64_t value_a{0};
  std::int64_t value_b{0};
};

/**
 * 固定容量、进程内的诊断 Trace 环形缓冲区。
 *
 * 容量在构造时一次分配。周期线程使用 try_lock 写入，若诊断读取暂时持锁则丢弃
 * Trace 并增加 dropped，而不是阻塞控制监督周期。Trace 丢失不得改变控制行为。
 */
class TraceBuffer {
 public:
  explicit TraceBuffer(std::size_t capacity);

  void record(const TraceEvent& event) noexcept;
  [[nodiscard]] std::vector<TraceEvent> snapshot() const;
  [[nodiscard]] std::size_t capacity() const noexcept;
  [[nodiscard]] std::uint64_t dropped() const noexcept;

 private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::vector<TraceEvent> storage_;
  std::size_t next_{0};
  std::size_t size_{0};
  std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace rcr
