#ifndef RCR_MCU_PLATFORM_H
#define RCR_MCU_PLATFORM_H

#include "rcr_mcu/can_v1.h"

#include <stdbool.h>
#include <stdint.h>

bool rcr_platform_init(void);
uint32_t rcr_platform_millis(void);
bool rcr_platform_next_session(uint16_t *session_id);

bool rcr_platform_can_rx_pop(rcr_can_frame_t *frame);
bool rcr_platform_can_tx_enqueue(const rcr_can_frame_t *frame);
void rcr_platform_can_tx_pump(void);
bool rcr_platform_take_rx_overflow(void);
bool rcr_platform_can_bus_off(void);

typedef enum {
  RCR_PLATFORM_PROBE_TX_NONE = 0,
  RCR_PLATFORM_PROBE_TX_OK = 1,
  RCR_PLATFORM_PROBE_TX_ARBITRATION_LOST = 2,
  RCR_PLATFORM_PROBE_TX_ERROR = 3,
} rcr_platform_probe_tx_result_t;

bool rcr_platform_arbitration_probe_begin(void);
rcr_platform_probe_tx_result_t rcr_platform_arbitration_probe_poll(void);
bool rcr_platform_arbitration_probe_try_send(uint32_t sequence);

void rcr_platform_set_output_led(bool enabled);
void rcr_platform_set_servo_pwm_us(uint16_t pulse_us);
void rcr_platform_watchdog_start(void);
void rcr_platform_watchdog_kick(void);

#endif
