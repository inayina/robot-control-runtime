#ifndef RCR_MCU_CAN_V1_H
#define RCR_MCU_CAN_V1_H

#include <stdbool.h>
#include <stdint.h>

#define RCR_CAN_V1_PROTOCOL_VERSION UINT8_C(1)
#define RCR_CAN_V1_NODE_ID UINT8_C(1)
#define RCR_CAN_V1_DLC UINT8_C(8)
/* NodeStatus.input_bits bit0：对射红外挡住 / 机构到位。不是 fault，不上灯控。 */
#define RCR_CAN_V1_INPUT_BIT_POSITION_REACHED UINT16_C(0x0001)

typedef enum {
  RCR_CAN_V1_APPLIED = 0,
  RCR_CAN_V1_STALE_SEQUENCE = 1,
  RCR_CAN_V1_SESSION_MISMATCH = 2,
  RCR_CAN_V1_EXPIRED = 3,
  RCR_CAN_V1_INVALID_MASK = 4,
  RCR_CAN_V1_NOT_READY = 5,
} rcr_can_v1_output_result_t;

typedef struct {
  uint32_t can_id;
  uint8_t dlc;
  uint8_t data[8];
  bool extended;
  bool rtr;
  uint32_t receive_ms;
} rcr_can_frame_t;

typedef struct {
  uint8_t node_id;
  uint16_t boot_id;
  uint16_t session_id;
  uint16_t heartbeat_sequence;
} rcr_can_v1_heartbeat_t;

typedef struct {
  uint8_t node_id;
  bool interlock_ready;
  uint16_t session_id;
  uint16_t input_bits;
  uint16_t fault_code;
} rcr_can_v1_node_status_t;

typedef struct {
  uint8_t node_id;
  uint8_t mask;
  uint16_t session_id;
  uint16_t sequence;
  uint8_t values;
  uint8_t validity_10ms;
} rcr_can_v1_output_command_t;

typedef struct {
  uint8_t node_id;
  rcr_can_v1_output_result_t result;
  uint16_t session_id;
  uint16_t sequence;
  uint8_t output_mirror;
} rcr_can_v1_output_status_t;

uint16_t rcr_can_v1_make_id(uint8_t function, uint8_t node_id);
bool rcr_can_v1_sequence_newer(uint16_t candidate, uint16_t previous);

bool rcr_can_v1_encode_heartbeat(const rcr_can_v1_heartbeat_t *message,
                                 rcr_can_frame_t *frame);
bool rcr_can_v1_encode_node_status(const rcr_can_v1_node_status_t *message,
                                   rcr_can_frame_t *frame);
bool rcr_can_v1_decode_output_command(const rcr_can_frame_t *frame,
                                      uint8_t expected_node_id,
                                      rcr_can_v1_output_command_t *message);
bool rcr_can_v1_encode_output_status(const rcr_can_v1_output_status_t *message,
                                     rcr_can_frame_t *frame);

#endif

