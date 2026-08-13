#include "rcr_mcu/can_v1.h"

#include <stddef.h>

enum {
  RCR_CAN_V1_FN_HEARTBEAT = 0x01,
  RCR_CAN_V1_FN_STATUS = 0x02,
  RCR_CAN_V1_FN_OUTPUT_COMMAND = 0x03,
  RCR_CAN_V1_FN_OUTPUT_STATUS = 0x04,
};

static void put_u16_be(uint8_t *destination, uint16_t value) {
  destination[0] = (uint8_t)(value >> 8U);
  destination[1] = (uint8_t)(value & UINT16_C(0x00FF));
}

static uint16_t get_u16_be(const uint8_t *source) {
  return (uint16_t)(((uint16_t)source[0] << 8U) | (uint16_t)source[1]);
}

static void clear_frame(rcr_can_frame_t *frame, uint16_t can_id) {
  frame->can_id = can_id;
  frame->dlc = RCR_CAN_V1_DLC;
  frame->extended = false;
  frame->rtr = false;
  frame->receive_ms = 0U;
  for (uint8_t index = 0U; index < RCR_CAN_V1_DLC; ++index) {
    frame->data[index] = 0U;
  }
}

uint16_t rcr_can_v1_make_id(uint8_t function, uint8_t node_id) {
  return (uint16_t)(((uint16_t)function << 5U) |
                    ((uint16_t)node_id & UINT16_C(0x001F)));
}

bool rcr_can_v1_sequence_newer(uint16_t candidate, uint16_t previous) {
  const uint16_t delta = (uint16_t)(candidate - previous);
  return candidate != previous && delta < UINT16_C(0x8000);
}

bool rcr_can_v1_encode_heartbeat(const rcr_can_v1_heartbeat_t *message,
                                 rcr_can_frame_t *frame) {
  if (message == NULL || frame == NULL || message->node_id == 0U ||
      message->node_id > 31U || message->boot_id == 0U ||
      message->session_id == 0U) {
    return false;
  }
  clear_frame(frame,
              rcr_can_v1_make_id(RCR_CAN_V1_FN_HEARTBEAT, message->node_id));
  frame->data[0] = RCR_CAN_V1_PROTOCOL_VERSION;
  put_u16_be(&frame->data[2], message->boot_id);
  put_u16_be(&frame->data[4], message->session_id);
  put_u16_be(&frame->data[6], message->heartbeat_sequence);
  return true;
}

bool rcr_can_v1_encode_node_status(const rcr_can_v1_node_status_t *message,
                                   rcr_can_frame_t *frame) {
  if (message == NULL || frame == NULL || message->node_id == 0U ||
      message->node_id > 31U || message->session_id == 0U) {
    return false;
  }
  clear_frame(frame,
              rcr_can_v1_make_id(RCR_CAN_V1_FN_STATUS, message->node_id));
  frame->data[0] = RCR_CAN_V1_PROTOCOL_VERSION;
  frame->data[1] = message->interlock_ready ? UINT8_C(1) : UINT8_C(0);
  put_u16_be(&frame->data[2], message->session_id);
  put_u16_be(&frame->data[4], message->input_bits);
  put_u16_be(&frame->data[6], message->fault_code);
  return true;
}

bool rcr_can_v1_decode_output_command(const rcr_can_frame_t *frame,
                                      uint8_t expected_node_id,
                                      rcr_can_v1_output_command_t *message) {
  if (frame == NULL || message == NULL || expected_node_id == 0U ||
      expected_node_id > 31U || frame->extended || frame->rtr ||
      frame->dlc != RCR_CAN_V1_DLC ||
      frame->can_id !=
          rcr_can_v1_make_id(RCR_CAN_V1_FN_OUTPUT_COMMAND, expected_node_id) ||
      frame->data[0] != RCR_CAN_V1_PROTOCOL_VERSION ||
      frame->data[1] == 0U) {
    return false;
  }

  const uint16_t session_id = get_u16_be(&frame->data[2]);
  const uint16_t sequence = get_u16_be(&frame->data[4]);
  const uint8_t validity_10ms = frame->data[7];
  if (session_id == 0U || sequence == 0U || validity_10ms == 0U ||
      validity_10ms > 250U) {
    return false;
  }

  message->node_id = expected_node_id;
  message->mask = frame->data[1];
  message->session_id = session_id;
  message->sequence = sequence;
  message->values = frame->data[6];
  message->validity_10ms = validity_10ms;
  return true;
}

bool rcr_can_v1_encode_output_status(const rcr_can_v1_output_status_t *message,
                                     rcr_can_frame_t *frame) {
  if (message == NULL || frame == NULL || message->node_id == 0U ||
      message->node_id > 31U || message->session_id == 0U ||
      (uint8_t)message->result > (uint8_t)RCR_CAN_V1_NOT_READY) {
    return false;
  }
  clear_frame(frame,
              rcr_can_v1_make_id(RCR_CAN_V1_FN_OUTPUT_STATUS, message->node_id));
  frame->data[0] = RCR_CAN_V1_PROTOCOL_VERSION;
  frame->data[1] = (uint8_t)message->result;
  put_u16_be(&frame->data[2], message->session_id);
  put_u16_be(&frame->data[4], message->sequence);
  frame->data[6] = message->output_mirror;
  return true;
}

