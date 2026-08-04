#include "rcr_multibus/observation.hpp"

#include <utility>

namespace rcr::multibus {

const char* to_string(SourceHealth health) noexcept {
  switch (health) {
    case SourceHealth::Waiting:
      return "waiting";
    case SourceHealth::Healthy:
      return "healthy";
    case SourceHealth::Faulted:
      return "faulted";
  }
  return "unknown";
}

void ObservationStore::mark_healthy(SourceStatus& status, std::int64_t now_ns) {
  status.health = SourceHealth::Healthy;
  ++status.updates;
  status.last_change_ns = now_ns;
  status.detail = "ok";
}

void ObservationStore::mark_failure(SourceStatus& status, std::int64_t now_ns,
                                    std::string detail) {
  status.health = SourceHealth::Faulted;
  ++status.failures;
  status.last_change_ns = now_ns;
  status.detail = std::move(detail);
}

void ObservationStore::publish_can(CanStatusSample sample) {
  std::lock_guard lock(mutex_);
  sample.valid = true;
  snapshot_.can = sample;
  mark_healthy(snapshot_.can_source, sample.sampled_ns);
}

void ObservationStore::publish_temperature(TemperatureSample sample) {
  std::lock_guard lock(mutex_);
  sample.valid = true;
  snapshot_.temperature = sample;
  mark_healthy(snapshot_.modbus_source, sample.sampled_ns);
}

void ObservationStore::mark_can_failure(std::int64_t now_ns, std::string detail) {
  std::lock_guard lock(mutex_);
  // 保留最后一个好样本用于诊断，但来源健康明确变为 faulted，调用方不能把旧值当新值。
  mark_failure(snapshot_.can_source, now_ns, std::move(detail));
}

void ObservationStore::mark_modbus_failure(std::int64_t now_ns, std::string detail) {
  std::lock_guard lock(mutex_);
  mark_failure(snapshot_.modbus_source, now_ns, std::move(detail));
}

ObservationSnapshot ObservationStore::snapshot() const {
  std::lock_guard lock(mutex_);
  return snapshot_;
}

bool sample_is_stale(bool valid, std::int64_t sampled_ns, std::int64_t now_ns,
                     std::int64_t max_age_ns) noexcept {
  if (!valid || sampled_ns <= 0 || now_ns < sampled_ns || max_age_ns < 0) {
    return true;
  }
  return now_ns - sampled_ns > max_age_ns;
}

}  // namespace rcr::multibus
