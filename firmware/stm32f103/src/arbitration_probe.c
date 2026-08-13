#include "rcr_mcu/platform.h"

#include <stdbool.h>
#include <stdint.h>

enum {
  RCR_PROBE_PERIOD_MS = 1U,
  RCR_PROBE_DURATION_MS = 30000U,
  RCR_PROBE_MAGIC = 0x41524231U, /* ASCII "ARB1" */
};

/*
 * 这些具名 volatile 变量是本次物理诊断的只读证据面。ST-Link 根据 ELF symbol 一次读出；
 * 正常 CAN V1 固件不链接本文件，也不会在协议帧中暴露这些实现细节。
 */
volatile uint32_t rcr_probe_magic = RCR_PROBE_MAGIC;
volatile uint32_t rcr_probe_attempts;
volatile uint32_t rcr_probe_tx_ok;
volatile uint32_t rcr_probe_arbitration_lost;
volatile uint32_t rcr_probe_tx_error;
volatile uint32_t rcr_probe_done;

static bool time_after_or_equal(uint32_t now_ms, uint32_t deadline_ms) {
  return (int32_t)(now_ms - deadline_ms) >= 0;
}

int main(void) {
  /* 强制 magic 留在 ELF/RAM，ST-Link 可据此拒绝读取错误固件的同一地址。 */
  (void)rcr_probe_magic;
  /* platform_init 保持 PC13 OFF、PA8 CCR1=0；诊断固件永不提交舵机 PWM。 */
  if (!rcr_platform_init() || !rcr_platform_arbitration_probe_begin()) {
    rcr_probe_tx_error = UINT32_MAX;
    rcr_probe_done = 1U;
    for (;;) {
    }
  }

  const uint32_t start_ms = rcr_platform_millis();
  uint32_t next_send_ms = start_ms;
  for (;;) {
    const rcr_platform_probe_tx_result_t result =
        rcr_platform_arbitration_probe_poll();
    if (result == RCR_PLATFORM_PROBE_TX_OK) {
      ++rcr_probe_tx_ok;
    } else if (result == RCR_PLATFORM_PROBE_TX_ARBITRATION_LOST) {
      ++rcr_probe_arbitration_lost;
    } else if (result == RCR_PLATFORM_PROBE_TX_ERROR) {
      ++rcr_probe_tx_error;
    }

    const uint32_t now_ms = rcr_platform_millis();
    if (!time_after_or_equal(now_ms, start_ms + RCR_PROBE_DURATION_MS)) {
      if (time_after_or_equal(now_ms, next_send_ms) &&
          rcr_platform_arbitration_probe_try_send(rcr_probe_attempts + 1U)) {
        ++rcr_probe_attempts;
        next_send_ms += RCR_PROBE_PERIOD_MS;
        if (time_after_or_equal(now_ms, next_send_ms)) {
          next_send_ms = now_ms + RCR_PROBE_PERIOD_MS;
        }
      }
    } else if (result == RCR_PLATFORM_PROBE_TX_NONE &&
               rcr_probe_attempts ==
                   rcr_probe_tx_ok + rcr_probe_arbitration_lost +
                       rcr_probe_tx_error) {
      rcr_probe_done = 1U;
    }
  }
}
