#ifndef RCR_MCU_INPUT_DEBOUNCE_H
#define RCR_MCU_INPUT_DEBOUNCE_H

#include <stdbool.h>
#include <stdint.h>

enum { RCR_INPUT_DEBOUNCE_MS = 20U };

/*
 * 极性必须来自实物 bring-up，禁止在未测量时写死 HIGH/LOW。
 * ACTIVE_HIGH = 遮挡时 PA0 IDR=1；ACTIVE_LOW = 遮挡时 PA0 IDR=0。
 * UNSET 时永远不报到位，避免把反极性当成 POSITION_REACHED。
 */
typedef enum {
  RCR_TARGET_SENSOR_POLARITY_UNSET = 0,
  RCR_TARGET_SENSOR_POLARITY_ACTIVE_HIGH = 1,
  RCR_TARGET_SENSOR_POLARITY_ACTIVE_LOW = 2,
} rcr_target_sensor_polarity_t;

#ifndef RCR_TARGET_SENSOR_POLARITY
/* 2026-08-16 实物：挡片遮挡对射槽时 PA0 = HIGH → ACTIVE_HIGH。
 * 无遮挡未单独口述，按数字互补推断为 LOW；若 HOME 时仍是 HIGH，必须改回 UNSET。 */
#define RCR_TARGET_SENSOR_POLARITY RCR_TARGET_SENSOR_POLARITY_ACTIVE_HIGH
#endif

typedef struct {
  bool armed;
  bool candidate_reached;
  bool stable_reached;
  uint32_t candidate_since_ms;
} rcr_input_debounce_t;

void rcr_input_debounce_init(rcr_input_debounce_t *state);

/* raw_high 是 PA0 电平，不是到位语义。UNSET 时返回 false。 */
bool rcr_target_sensor_active(bool raw_high,
                              rcr_target_sensor_polarity_t polarity);

/*
 * reached 已按极性归一化：true = 对射被挡 / 机构到位。
 * 稳定值只在同一值连续保持 debounce_ms 后提交；短毛刺不改 bit0。
 * 上电默认未到位，避免启动瞬间误报 POSITION_REACHED。
 */
bool rcr_input_debounce_update(rcr_input_debounce_t *state, bool reached,
                               uint32_t now_ms, uint32_t debounce_ms);

uint16_t rcr_input_bits_from_reached(bool reached);

#endif
