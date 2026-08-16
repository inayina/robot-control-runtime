#include "rcr_mcu/can_v1.h"
#include "rcr_mcu/input_debounce.h"
#include "rcr_mcu/node.h"
#include "rcr_mcu/servo_pwm.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define EXPECT(condition)                                                       \
  do {                                                                          \
    if (!(condition)) {                                                         \
      fprintf(stderr, "%s:%d EXPECT failed: %s\n", __FILE__, __LINE__,        \
              #condition);                                                      \
      ++failures;                                                               \
    }                                                                           \
  } while (0)

static rcr_can_frame_t make_command(uint16_t session, uint16_t sequence,
                                    uint8_t mask, uint8_t values,
                                    uint8_t validity_10ms,
                                    uint32_t receive_ms) {
  rcr_can_frame_t frame = {0};
  frame.can_id = UINT32_C(0x061);
  frame.dlc = 8U;
  frame.receive_ms = receive_ms;
  frame.data[0] = 1U;
  frame.data[1] = mask;
  frame.data[2] = (uint8_t)(session >> 8U);
  frame.data[3] = (uint8_t)session;
  frame.data[4] = (uint8_t)(sequence >> 8U);
  frame.data[5] = (uint8_t)sequence;
  frame.data[6] = values;
  frame.data[7] = validity_10ms;
  return frame;
}

static void expect_data(const rcr_can_frame_t *frame, uint16_t can_id,
                        const uint8_t expected[8]) {
  EXPECT(frame->can_id == can_id);
  EXPECT(frame->dlc == 8U);
  EXPECT(!frame->extended);
  EXPECT(!frame->rtr);
  EXPECT(memcmp(frame->data, expected, 8U) == 0);
}

static void test_golden_encodes(void) {
  rcr_can_frame_t frame;
  const rcr_can_v1_heartbeat_t heartbeat = {1U, 2U, 10U, 256U};
  const uint8_t heartbeat_bytes[8] = {0x01U, 0x00U, 0x00U, 0x02U,
                                      0x00U, 0x0AU, 0x01U, 0x00U};
  EXPECT(rcr_can_v1_encode_heartbeat(&heartbeat, &frame));
  expect_data(&frame, 0x021U, heartbeat_bytes);

  const rcr_can_v1_node_status_t status = {1U, true, 10U, 3U, 6U};
  const uint8_t status_bytes[8] = {0x01U, 0x01U, 0x00U, 0x0AU,
                                   0x00U, 0x03U, 0x00U, 0x06U};
  EXPECT(rcr_can_v1_encode_node_status(&status, &frame));
  expect_data(&frame, 0x041U, status_bytes);

  const rcr_can_v1_output_status_t output = {
      1U, RCR_CAN_V1_APPLIED, 10U, 5U, 5U};
  const uint8_t output_bytes[8] = {0x01U, 0x00U, 0x00U, 0x0AU,
                                   0x00U, 0x05U, 0x05U, 0x00U};
  EXPECT(rcr_can_v1_encode_output_status(&output, &frame));
  expect_data(&frame, 0x081U, output_bytes);
}

static void test_command_decode_and_rejects(void) {
  rcr_can_frame_t frame = make_command(10U, 5U, 0x0FU, 0x05U, 10U, 7U);
  rcr_can_v1_output_command_t command;
  EXPECT(rcr_can_v1_decode_output_command(&frame, 1U, &command));
  EXPECT(command.mask == 0x0FU);
  EXPECT(command.session_id == 10U);
  EXPECT(command.sequence == 5U);
  EXPECT(command.values == 5U);
  EXPECT(command.validity_10ms == 10U);

  frame.data[0] = 2U;
  EXPECT(!rcr_can_v1_decode_output_command(&frame, 1U, &command));
  frame.data[0] = 1U;
  frame.data[1] = 0U;
  EXPECT(!rcr_can_v1_decode_output_command(&frame, 1U, &command));
  frame.data[1] = 1U;
  frame.data[7] = 251U;
  EXPECT(!rcr_can_v1_decode_output_command(&frame, 1U, &command));
  frame.data[7] = 10U;
  frame.extended = true;
  EXPECT(!rcr_can_v1_decode_output_command(&frame, 1U, &command));

  EXPECT(rcr_can_v1_sequence_newer(1U, UINT16_C(0xFFFF)));
  EXPECT(!rcr_can_v1_sequence_newer(UINT16_C(0x8001), 1U));
}

static void test_node_lease_and_rejections(void) {
  rcr_node_t node;
  rcr_node_init(&node, 1U, 9U, 10U, true);
  rcr_can_v1_output_status_t status;
  rcr_can_frame_t frame = make_command(10U, 1U, 0x0FU, 0x05U, 10U, 100U);
  EXPECT(rcr_node_on_frame(&node, &frame, 100U, &status) ==
         RCR_NODE_FRAME_STATUS_READY);
  EXPECT(status.result == RCR_CAN_V1_APPLIED);
  EXPECT(node.output_bits == 0x05U);
  EXPECT(node.lease_deadline_ms == 200U);

  frame = make_command(10U, 1U, 0x01U, 0x00U, 20U, 150U);
  EXPECT(rcr_node_on_frame(&node, &frame, 150U, &status) ==
         RCR_NODE_FRAME_STATUS_READY);
  EXPECT(status.result == RCR_CAN_V1_STALE_SEQUENCE);
  EXPECT(node.lease_deadline_ms == 200U);
  EXPECT(rcr_node_expire_lease(&node, 200U));
  EXPECT(node.output_bits == 0U);

  frame = make_command(11U, 2U, 0x01U, 0x01U, 10U, 210U);
  EXPECT(rcr_node_on_frame(&node, &frame, 210U, &status) ==
         RCR_NODE_FRAME_STATUS_READY);
  EXPECT(status.result == RCR_CAN_V1_SESSION_MISMATCH);
  EXPECT(node.output_bits == 0U);
}

static void test_partial_mask_wrap_and_fault(void) {
  rcr_node_t node;
  rcr_node_init(&node, 1U, 1U, 1U, true);
  rcr_can_v1_output_status_t status;
  rcr_can_frame_t frame = make_command(1U, 0xFFFFU, 0x0FU, 0x0AU, 30U, 0U);
  EXPECT(rcr_node_on_frame(&node, &frame, 0U, &status) ==
         RCR_NODE_FRAME_STATUS_READY);
  EXPECT(node.output_bits == 0x0AU);
  frame = make_command(1U, 1U, 0xF0U, 0xB0U, 30U, 1U);
  EXPECT(rcr_node_on_frame(&node, &frame, 1U, &status) ==
         RCR_NODE_FRAME_STATUS_READY);
  EXPECT(status.result == RCR_CAN_V1_APPLIED);
  EXPECT(node.output_bits == 0xBAU);

  rcr_node_set_bus_ready(&node, false);
  EXPECT(node.output_bits == 0U);
  EXPECT(!rcr_node_interlock_ready(&node));
  EXPECT(rcr_node_fault_code(&node) == RCR_NODE_FAULT_COMM_LOSS);
  rcr_node_set_bus_ready(&node, true);
  EXPECT(rcr_node_interlock_ready(&node));
  EXPECT(node.output_bits == 0U);

  rcr_node_latch_internal_fault(&node);
  EXPECT(!rcr_node_interlock_ready(&node));
  EXPECT(rcr_node_fault_code(&node) == RCR_NODE_FAULT_INTERNAL);
}

static void test_expired_and_illegal_command(void) {
  rcr_node_t node;
  rcr_node_init(&node, 1U, 1U, 1U, true);
  rcr_can_v1_output_status_t status;
  rcr_can_v1_output_command_t command = {1U, 1U, 1U, 1U, 1U, 1U};
  EXPECT(rcr_node_apply_command(&node, &command, 0U, 10U, &status) ==
         RCR_NODE_FRAME_STATUS_READY);
  EXPECT(status.result == RCR_CAN_V1_EXPIRED);

  rcr_can_frame_t frame = make_command(1U, 1U, 0U, 1U, 10U, 0U);
  EXPECT(rcr_node_on_frame(&node, &frame, 0U, &status) ==
         RCR_NODE_FRAME_REJECTED);
  EXPECT(node.protocol_rejects == 1U);

  // receive+validity 跨 uint32_t 毫秒回绕时仍按有符号环差在 deadline 边界失效。
  command.sequence = 2U;
  EXPECT(rcr_node_apply_command(&node, &command, UINT32_MAX - 5U, 0U,
                                &status) == RCR_NODE_FRAME_STATUS_READY);
  EXPECT(status.result == RCR_CAN_V1_APPLIED);
  EXPECT(!rcr_node_expire_lease(&node, 3U));
  EXPECT(rcr_node_expire_lease(&node, 4U));
}

static void test_servo_pwm_mapping(void) {
  EXPECT(rcr_servo_pwm_target_us(false, 0U) == RCR_SERVO_PWM_DISABLED_US);
  EXPECT(rcr_servo_pwm_target_us(false, 1U) == RCR_SERVO_PWM_DISABLED_US);

  /* 位置 A 的 mirror 为 0，但有效 lease 必须生成 1.25ms，而不是误判为关闭。 */
  EXPECT(rcr_servo_pwm_target_us(true, 0U) == RCR_SERVO_POSITION_A_US);
  EXPECT(rcr_servo_pwm_target_us(true, UINT8_C(0xFE)) ==
         RCR_SERVO_POSITION_A_US);
  EXPECT(rcr_servo_pwm_target_us(true, 1U) == RCR_SERVO_POSITION_B_US);
  EXPECT(rcr_servo_pwm_target_us(true, UINT8_C(0xFF)) ==
         RCR_SERVO_POSITION_B_US);
}

static void test_status_carries_input_bits_without_fault(void) {
  rcr_node_t node;
  rcr_node_init(&node, 1U, 1U, 1U, true);
  EXPECT(node.input_bits == 0U);
  rcr_can_v1_node_status_t status = rcr_node_make_status(&node);
  EXPECT(status.input_bits == 0U);
  EXPECT(status.fault_code == (uint16_t)RCR_NODE_FAULT_NONE);

  node.input_bits = rcr_input_bits_from_reached(true);
  status = rcr_node_make_status(&node);
  EXPECT(status.input_bits == RCR_CAN_V1_INPUT_BIT_POSITION_REACHED);
  EXPECT(status.fault_code == (uint16_t)RCR_NODE_FAULT_NONE);
  EXPECT(rcr_node_interlock_ready(&node));

  rcr_can_frame_t frame;
  EXPECT(rcr_can_v1_encode_node_status(&status, &frame));
  EXPECT(frame.data[4] == 0x00U);
  EXPECT(frame.data[5] == 0x01U);
  EXPECT(frame.data[6] == 0x00U);
  EXPECT(frame.data[7] == 0x00U);
}

static void test_input_debounce_requires_stable_window(void) {
  rcr_input_debounce_t debounce;
  rcr_input_debounce_init(&debounce);
  EXPECT(!rcr_input_debounce_update(&debounce, true, 0U, 20U));
  EXPECT(!rcr_input_debounce_update(&debounce, true, 19U, 20U));
  EXPECT(rcr_input_debounce_update(&debounce, true, 20U, 20U));
  EXPECT(rcr_input_bits_from_reached(true) ==
         RCR_CAN_V1_INPUT_BIT_POSITION_REACHED);

  EXPECT(rcr_input_debounce_update(&debounce, false, 21U, 20U));
  EXPECT(rcr_input_debounce_update(&debounce, false, 40U, 20U));
  EXPECT(!rcr_input_debounce_update(&debounce, false, 41U, 20U));
}

static void test_input_debounce_rejects_short_glitch(void) {
  rcr_input_debounce_t debounce;
  rcr_input_debounce_init(&debounce);
  EXPECT(!rcr_input_debounce_update(&debounce, true, 0U, 20U));
  EXPECT(!rcr_input_debounce_update(&debounce, false, 5U, 20U));
  EXPECT(!rcr_input_debounce_update(&debounce, true, 6U, 20U));
  EXPECT(!rcr_input_debounce_update(&debounce, true, 25U, 20U));
  EXPECT(rcr_input_debounce_update(&debounce, true, 26U, 20U));
}

static void test_target_sensor_polarity_unset_never_reached(void) {
  EXPECT(!rcr_target_sensor_active(true, RCR_TARGET_SENSOR_POLARITY_UNSET));
  EXPECT(!rcr_target_sensor_active(false, RCR_TARGET_SENSOR_POLARITY_UNSET));
  EXPECT(!rcr_target_sensor_active(true, (rcr_target_sensor_polarity_t)99));
}

static void test_target_sensor_polarity_maps_raw_only(void) {
  EXPECT(rcr_target_sensor_active(true, RCR_TARGET_SENSOR_POLARITY_ACTIVE_HIGH));
  EXPECT(
      !rcr_target_sensor_active(false, RCR_TARGET_SENSOR_POLARITY_ACTIVE_HIGH));
  EXPECT(!rcr_target_sensor_active(true, RCR_TARGET_SENSOR_POLARITY_ACTIVE_LOW));
  EXPECT(rcr_target_sensor_active(false, RCR_TARGET_SENSOR_POLARITY_ACTIVE_LOW));
}

static void test_input_debounce_millis_wrap(void) {
  rcr_input_debounce_t debounce;
  rcr_input_debounce_init(&debounce);
  EXPECT(!rcr_input_debounce_update(&debounce, true, UINT32_MAX - 5U, 20U));
  EXPECT(rcr_input_debounce_update(&debounce, true, 14U, 20U));
}

int main(void) {
  test_golden_encodes();
  test_command_decode_and_rejects();
  test_node_lease_and_rejections();
  test_partial_mask_wrap_and_fault();
  test_expired_and_illegal_command();
  test_servo_pwm_mapping();
  test_status_carries_input_bits_without_fault();
  test_input_debounce_requires_stable_window();
  test_input_debounce_rejects_short_glitch();
  test_input_debounce_millis_wrap();
  test_target_sensor_polarity_unset_never_reached();
  test_target_sensor_polarity_maps_raw_only();
  if (failures != 0) {
    fprintf(stderr, "%d test expectation(s) failed\n", failures);
    return 1;
  }
  puts("STM32F103 CAN V1 logic tests passed");
  return 0;
}
