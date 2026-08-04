#pragma once

// 实验性数据汇聚层：保存已经由各协议层解码完成的类型化观测。
// 它不打开 fd、不调度事务，也不向 Runtime Core 注入控制决策。

#include <cstdint>
#include <mutex>
#include <string>

namespace rcr::multibus {

enum class SourceHealth : std::uint8_t {
  Waiting = 0,
  Healthy = 1,
  Faulted = 2,
};

[[nodiscard]] const char* to_string(SourceHealth health) noexcept;

struct SourceStatus {
  SourceHealth health{SourceHealth::Waiting};
  std::uint64_t updates{0};
  std::uint64_t failures{0};
  std::int64_t last_change_ns{0};
  std::string detail{"waiting for first sample"};
};

struct CanStatusSample {
  bool valid{false};
  std::int64_t sampled_ns{0};
  std::uint8_t node_id{0};
  std::uint16_t session_id{0};
  bool interlock_ready{false};
  std::uint16_t input_bits{0};
  std::uint16_t fault_code{0};
};

struct TemperatureSample {
  bool valid{false};
  std::int64_t sampled_ns{0};
  std::uint16_t register_address{0};
  std::int16_t deci_celsius{0};
};

struct ObservationSnapshot {
  CanStatusSample can{};
  TemperatureSample temperature{};
  SourceStatus can_source{};
  SourceStatus modbus_source{};
};

/**
 * 两个 worker 共享的最新观测快照。
 *
 * 单个 mutex 让“样本值 + 来源健康 + 计数”作为一致快照发布。这里的数据频率只有
 * CAN 状态约 10 Hz 与 Modbus 约 10 Hz；相比无锁双缓冲，互斥锁更容易证明没有撕裂读取，
 * 而且本对象不进入 Runtime 的周期控制线程。
 */
class ObservationStore {
 public:
  void publish_can(CanStatusSample sample);
  void publish_temperature(TemperatureSample sample);
  void mark_can_failure(std::int64_t now_ns, std::string detail);
  void mark_modbus_failure(std::int64_t now_ns, std::string detail);

  [[nodiscard]] ObservationSnapshot snapshot() const;

 private:
  static void mark_healthy(SourceStatus& status, std::int64_t now_ns);
  static void mark_failure(SourceStatus& status, std::int64_t now_ns, std::string detail);

  mutable std::mutex mutex_;
  ObservationSnapshot snapshot_{};
};

[[nodiscard]] bool sample_is_stale(bool valid, std::int64_t sampled_ns,
                                   std::int64_t now_ns,
                                   std::int64_t max_age_ns) noexcept;

}  // namespace rcr::multibus
