#include "rcr_mcu/can_v1.h"
#include "rcr_mcu/node.h"
#include "rcr_mcu/platform.h"
#include "rcr_mcu/servo_pwm.h"

#include <stdbool.h>
#include <stdint.h>

enum { RCR_PERIOD_MS = 100U };

static bool time_after_or_equal(uint32_t now_ms, uint32_t deadline_ms) {
  return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void enqueue_or_latch(rcr_node_t *node, const rcr_can_frame_t *frame) {
  if (!rcr_platform_can_tx_enqueue(frame)) {
    // 周期状态或命令应答都不能被无声覆盖；队列满时输出归零并锁存内部故障。
    rcr_node_latch_internal_fault(node);
  }
}

static void publish_heartbeat(rcr_node_t *node) {
  const rcr_can_v1_heartbeat_t heartbeat = rcr_node_make_heartbeat(node);
  rcr_can_frame_t frame;
  if (!rcr_can_v1_encode_heartbeat(&heartbeat, &frame)) {
    rcr_node_latch_internal_fault(node);
    return;
  }
  enqueue_or_latch(node, &frame);
}

static void publish_status(rcr_node_t *node) {
  const rcr_can_v1_node_status_t status = rcr_node_make_status(node);
  rcr_can_frame_t frame;
  if (!rcr_can_v1_encode_node_status(&status, &frame)) {
    rcr_node_latch_internal_fault(node);
    return;
  }
  enqueue_or_latch(node, &frame);
}

int main(void) {
  // 初始化先把 PC13 和 PA8 保持为无输出；HSE/CAN 失败不会留下旧目标。
  if (!rcr_platform_init()) {
    for (;;) {
    }
  }

  uint16_t session_id = 1U;
  const bool identity_ready = rcr_platform_next_session(&session_id);
  rcr_node_t node;
  rcr_node_init(&node, RCR_CAN_V1_NODE_ID, session_id, session_id,
                identity_ready);

  rcr_platform_watchdog_start();
  uint32_t next_heartbeat_ms = rcr_platform_millis();
  uint32_t next_status_ms = next_heartbeat_ms;
  uint16_t applied_servo_pulse_us = RCR_SERVO_PWM_DISABLED_US;
  bool bus_off_previous = false;

  for (;;) {
    const uint32_t now_ms = rcr_platform_millis();

    if (rcr_platform_take_rx_overflow()) {
      rcr_node_latch_internal_fault(&node);
    }

    const bool bus_off = rcr_platform_can_bus_off();
    if (bus_off != bus_off_previous) {
      rcr_node_set_bus_ready(&node, !bus_off);
      bus_off_previous = bus_off;
    }

    rcr_can_frame_t received;
    while (rcr_platform_can_rx_pop(&received)) {
      const uint32_t apply_ms = rcr_platform_millis();
      rcr_can_v1_output_status_t output_status;
      const rcr_node_frame_result_t result =
          rcr_node_on_frame(&node, &received, apply_ms, &output_status);
      if (result == RCR_NODE_FRAME_STATUS_READY) {
        rcr_can_frame_t response;
        if (rcr_can_v1_encode_output_status(&output_status, &response)) {
          enqueue_or_latch(&node, &response);
        } else {
          rcr_node_latch_internal_fault(&node);
        }
      }
    }

    (void)rcr_node_expire_lease(&node, now_ms);

    if (time_after_or_equal(now_ms, next_heartbeat_ms)) {
      publish_heartbeat(&node);
      next_heartbeat_ms += RCR_PERIOD_MS;
      if (time_after_or_equal(now_ms, next_heartbeat_ms)) {
        next_heartbeat_ms = now_ms + RCR_PERIOD_MS;
      }
    }
    if (time_after_or_equal(now_ms, next_status_ms)) {
      publish_status(&node);
      next_status_ms += RCR_PERIOD_MS;
      if (time_after_or_equal(now_ms, next_status_ms)) {
        next_status_ms = now_ms + RCR_PERIOD_MS;
      }
    }

    /* 周期上报入队也可能锁存故障；在所有状态变化之后统一提交物理输出。 */
    rcr_platform_set_output_led((node.output_bits & UINT8_C(1)) != 0U);
    const uint16_t target_servo_pulse_us =
        rcr_servo_pwm_target_us(node.lease_active, node.output_bits);
    if (target_servo_pulse_us != applied_servo_pulse_us) {
      rcr_platform_set_servo_pwm_us(target_servo_pulse_us);
      applied_servo_pulse_us = target_servo_pulse_us;
    }

    rcr_platform_can_tx_pump();
    rcr_platform_watchdog_kick();
  }
}
