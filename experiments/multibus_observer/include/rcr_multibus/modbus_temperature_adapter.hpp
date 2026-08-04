#pragma once

// 设备合同→数据窄适配：本 Demo 明确把一个 Holding Register 解释为温度。
// 它不发 Modbus 事务；不同设备若字节序/缩放不同，应提供不同映射而非改“通用协议类型”。

#include "rcr_multibus/observation.hpp"

#include <cstdint>

namespace rcr::multibus {

/**
 * Demo 的明确设备合同：一个 Holding Register 是有符号 int16，单位为 0.1 摄氏度。
 * 这不是 Modbus 协议的通用 float/温度规则，换设备时必须换映射代码或配置合同。
 */
[[nodiscard]] TemperatureSample decode_temperature_register(std::uint16_t raw_register,
                                                            std::uint16_t address,
                                                            std::int64_t sampled_ns) noexcept;

}  // namespace rcr::multibus

