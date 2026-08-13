#ifndef RCR_MCU_SERVO_PWM_H
#define RCR_MCU_SERVO_PWM_H

#include <stdbool.h>
#include <stdint.h>

enum {
  RCR_SERVO_PWM_DISABLED_US = 0,
  RCR_SERVO_POSITION_A_US = 1250,
  RCR_SERVO_POSITION_B_US = 1750,
};

// 这是当前 SG90 双位置实验的具体设备语义，不是通用角度或位置反馈接口。
uint16_t rcr_servo_pwm_target_us(bool lease_active, uint8_t output_bits);

#endif
