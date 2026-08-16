#include "rcr_mcu/input_debounce.h"

#include "rcr_mcu/can_v1.h"

#include <stddef.h>

void rcr_input_debounce_init(rcr_input_debounce_t *state) {
  if (state == NULL) {
    return;
  }
  state->armed = false;
  state->candidate_reached = false;
  state->stable_reached = false;
  state->candidate_since_ms = 0U;
}

bool rcr_target_sensor_active(bool raw_high,
                              rcr_target_sensor_polarity_t polarity) {
  if (polarity == RCR_TARGET_SENSOR_POLARITY_ACTIVE_HIGH) {
    return raw_high;
  }
  if (polarity == RCR_TARGET_SENSOR_POLARITY_ACTIVE_LOW) {
    return !raw_high;
  }
  /* UNSET 或未知值：不得把任意电平解释为到位。 */
  return false;
}

bool rcr_input_debounce_update(rcr_input_debounce_t *state, bool reached,
                               uint32_t now_ms, uint32_t debounce_ms) {
  if (state == NULL) {
    return false;
  }
  if (!state->armed || reached != state->candidate_reached) {
    // 极性翻转或首样本：重新计时，保留上一次已提交值。
    state->armed = true;
    state->candidate_reached = reached;
    state->candidate_since_ms = now_ms;
    return state->stable_reached;
  }
  if ((int32_t)(now_ms - state->candidate_since_ms) >= (int32_t)debounce_ms) {
    state->stable_reached = state->candidate_reached;
  }
  return state->stable_reached;
}

uint16_t rcr_input_bits_from_reached(bool reached) {
  return reached ? RCR_CAN_V1_INPUT_BIT_POSITION_REACHED : UINT16_C(0);
}
