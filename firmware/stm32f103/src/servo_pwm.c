#include "rcr_mcu/servo_pwm.h"

uint16_t rcr_servo_pwm_target_us(bool lease_active, uint8_t output_bits) {
  if (!lease_active) {
    return RCR_SERVO_PWM_DISABLED_US;
  }
  return (output_bits & UINT8_C(1)) != 0U ? RCR_SERVO_POSITION_B_US
                                          : RCR_SERVO_POSITION_A_US;
}
