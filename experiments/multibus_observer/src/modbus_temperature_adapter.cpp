#include "rcr_multibus/modbus_temperature_adapter.hpp"

#include <bit>

namespace rcr::multibus {

TemperatureSample decode_temperature_register(std::uint16_t raw_register,
                                              std::uint16_t address,
                                              std::int64_t sampled_ns) noexcept {
  TemperatureSample sample{};
  sample.sampled_ns = sampled_ns;
  sample.register_address = address;
  // bit_cast 保留线上 16 bit 补码位型；避免依赖超范围 unsigned→signed 转换的实现定义行为。
  sample.deci_celsius = std::bit_cast<std::int16_t>(raw_register);
  sample.valid = true;
  return sample;
}

}  // namespace rcr::multibus

