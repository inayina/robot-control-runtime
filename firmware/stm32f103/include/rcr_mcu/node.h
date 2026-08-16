#ifndef RCR_MCU_NODE_H
#define RCR_MCU_NODE_H

#include "rcr_mcu/can_v1.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  RCR_NODE_FAULT_NONE = 0,
  RCR_NODE_FAULT_WATCHDOG = 1,
  RCR_NODE_FAULT_INPUT = 2,
  RCR_NODE_FAULT_COMM_LOSS = 3,
  RCR_NODE_FAULT_NODE = 4,
  RCR_NODE_FAULT_PROTOCOL_REJECT = 5,
  RCR_NODE_FAULT_INTERLOCK_LOST = 6,
  RCR_NODE_FAULT_INTERNAL = 7,
} rcr_node_fault_t;

typedef enum {
  RCR_NODE_FRAME_IGNORED = 0,
  RCR_NODE_FRAME_REJECTED = 1,
  RCR_NODE_FRAME_STATUS_READY = 2,
} rcr_node_frame_result_t;

typedef struct {
  uint8_t node_id;
  uint16_t boot_id;
  uint16_t session_id;
  uint16_t heartbeat_sequence;
  /* 由平台 GPIO 写入；bit0 = POSITION_REACHED，见 protocol/can_v1 §6.2.1。 */
  uint16_t input_bits;
  uint8_t output_bits;
  uint16_t last_accepted_sequence;
  uint32_t lease_deadline_ms;
  uint32_t protocol_rejects;
  bool has_accepted_sequence;
  bool lease_active;
  bool identity_ready;
  bool bus_ready;
  bool fatal_latched;
} rcr_node_t;

void rcr_node_init(rcr_node_t *node, uint8_t node_id, uint16_t boot_id,
                   uint16_t session_id, bool identity_ready);
void rcr_node_set_bus_ready(rcr_node_t *node, bool ready);
void rcr_node_latch_internal_fault(rcr_node_t *node);
bool rcr_node_interlock_ready(const rcr_node_t *node);
uint16_t rcr_node_fault_code(const rcr_node_t *node);

bool rcr_node_expire_lease(rcr_node_t *node, uint32_t now_ms);
rcr_node_frame_result_t rcr_node_on_frame(
    rcr_node_t *node, const rcr_can_frame_t *frame, uint32_t now_ms,
    rcr_can_v1_output_status_t *output_status);
rcr_node_frame_result_t rcr_node_apply_command(
    rcr_node_t *node, const rcr_can_v1_output_command_t *command,
    uint32_t receive_ms, uint32_t now_ms,
    rcr_can_v1_output_status_t *output_status);

rcr_can_v1_heartbeat_t rcr_node_make_heartbeat(rcr_node_t *node);
rcr_can_v1_node_status_t rcr_node_make_status(const rcr_node_t *node);

#endif

