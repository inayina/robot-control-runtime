#include "rcr_mcu/node.h"

#include <stddef.h>

enum { RCR_CAN_V1_FN_OUTPUT_COMMAND = 0x03 };

static bool time_after_or_equal(uint32_t now_ms, uint32_t deadline_ms) {
  return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void neutralize_output(rcr_node_t *node) {
  node->output_bits = 0U;
  node->lease_active = false;
  node->lease_deadline_ms = 0U;
}

void rcr_node_init(rcr_node_t *node, uint8_t node_id, uint16_t boot_id,
                   uint16_t session_id, bool identity_ready) {
  if (node == NULL) {
    return;
  }
  node->node_id = node_id == 0U ? 1U : node_id;
  node->boot_id = boot_id == 0U ? 1U : boot_id;
  node->session_id = session_id == 0U ? 1U : session_id;
  node->heartbeat_sequence = 0U;
  node->input_bits = 0U;
  node->output_bits = 0U;
  node->last_accepted_sequence = 0U;
  node->lease_deadline_ms = 0U;
  node->protocol_rejects = 0U;
  node->has_accepted_sequence = false;
  node->lease_active = false;
  node->identity_ready = identity_ready;
  node->bus_ready = true;
  node->fatal_latched = !identity_ready;
}

void rcr_node_set_bus_ready(rcr_node_t *node, bool ready) {
  if (node == NULL) {
    return;
  }
  node->bus_ready = ready;
  if (!ready) {
    // bus-off 之后旧输出授权立即作废；控制器恢复也不能自动重放旧目标。
    neutralize_output(node);
  }
}

void rcr_node_latch_internal_fault(rcr_node_t *node) {
  if (node == NULL) {
    return;
  }
  node->fatal_latched = true;
  neutralize_output(node);
}

bool rcr_node_interlock_ready(const rcr_node_t *node) {
  return node != NULL && node->identity_ready && node->bus_ready &&
         !node->fatal_latched;
}

uint16_t rcr_node_fault_code(const rcr_node_t *node) {
  if (node == NULL || node->fatal_latched || !node->identity_ready) {
    return (uint16_t)RCR_NODE_FAULT_INTERNAL;
  }
  if (!node->bus_ready) {
    return (uint16_t)RCR_NODE_FAULT_COMM_LOSS;
  }
  return (uint16_t)RCR_NODE_FAULT_NONE;
}

bool rcr_node_expire_lease(rcr_node_t *node, uint32_t now_ms) {
  if (node == NULL || !node->lease_active ||
      !time_after_or_equal(now_ms, node->lease_deadline_ms)) {
    return false;
  }
  // deadline 是半开区间；到期只归零输出，不清已接受序号。
  neutralize_output(node);
  return true;
}

rcr_node_frame_result_t rcr_node_apply_command(
    rcr_node_t *node, const rcr_can_v1_output_command_t *command,
    uint32_t receive_ms, uint32_t now_ms,
    rcr_can_v1_output_status_t *output_status) {
  if (node == NULL || command == NULL || output_status == NULL) {
    return RCR_NODE_FRAME_REJECTED;
  }

  (void)rcr_node_expire_lease(node, now_ms);
  output_status->node_id = node->node_id;
  output_status->session_id = node->session_id;
  output_status->sequence = command->sequence;
  output_status->output_mirror = node->output_bits;

  // 拒绝顺序与冻结合同一致；拒绝不能推进 sequence 或刷新 lease。
  if (command->session_id != node->session_id) {
    output_status->result = RCR_CAN_V1_SESSION_MISMATCH;
    return RCR_NODE_FRAME_STATUS_READY;
  }
  if (!rcr_node_interlock_ready(node)) {
    output_status->result = RCR_CAN_V1_NOT_READY;
    return RCR_NODE_FRAME_STATUS_READY;
  }
  if (node->has_accepted_sequence &&
      !rcr_can_v1_sequence_newer(command->sequence,
                                 node->last_accepted_sequence)) {
    output_status->result = RCR_CAN_V1_STALE_SEQUENCE;
    return RCR_NODE_FRAME_STATUS_READY;
  }

  const uint32_t deadline_ms =
      receive_ms + ((uint32_t)command->validity_10ms * UINT32_C(10));
  if (time_after_or_equal(now_ms, deadline_ms)) {
    output_status->result = RCR_CAN_V1_EXPIRED;
    return RCR_NODE_FRAME_STATUS_READY;
  }

  const uint8_t keep_mask = (uint8_t)(~command->mask);
  node->output_bits = (uint8_t)((node->output_bits & keep_mask) |
                                (command->values & command->mask));
  node->last_accepted_sequence = command->sequence;
  node->has_accepted_sequence = true;
  node->lease_active = true;
  node->lease_deadline_ms = deadline_ms;
  output_status->result = RCR_CAN_V1_APPLIED;
  output_status->output_mirror = node->output_bits;
  return RCR_NODE_FRAME_STATUS_READY;
}

rcr_node_frame_result_t rcr_node_on_frame(
    rcr_node_t *node, const rcr_can_frame_t *frame, uint32_t now_ms,
    rcr_can_v1_output_status_t *output_status) {
  if (node == NULL || frame == NULL || output_status == NULL) {
    return RCR_NODE_FRAME_REJECTED;
  }
  const uint16_t command_id =
      rcr_can_v1_make_id(RCR_CAN_V1_FN_OUTPUT_COMMAND, node->node_id);
  if (frame->can_id != command_id) {
    return RCR_NODE_FRAME_IGNORED;
  }

  rcr_can_v1_output_command_t command;
  if (!rcr_can_v1_decode_output_command(frame, node->node_id, &command)) {
    ++node->protocol_rejects;
    return RCR_NODE_FRAME_REJECTED;
  }
  return rcr_node_apply_command(node, &command, frame->receive_ms, now_ms,
                                output_status);
}

rcr_can_v1_heartbeat_t rcr_node_make_heartbeat(rcr_node_t *node) {
  rcr_can_v1_heartbeat_t heartbeat = {0};
  if (node == NULL) {
    return heartbeat;
  }
  heartbeat.node_id = node->node_id;
  heartbeat.boot_id = node->boot_id;
  heartbeat.session_id = node->session_id;
  heartbeat.heartbeat_sequence = node->heartbeat_sequence;
  node->heartbeat_sequence = (uint16_t)(node->heartbeat_sequence + 1U);
  return heartbeat;
}

rcr_can_v1_node_status_t rcr_node_make_status(const rcr_node_t *node) {
  rcr_can_v1_node_status_t status = {0};
  if (node == NULL) {
    return status;
  }
  status.node_id = node->node_id;
  status.interlock_ready = rcr_node_interlock_ready(node);
  status.session_id = node->session_id;
  status.input_bits = node->input_bits;
  status.fault_code = rcr_node_fault_code(node);
  return status;
}

