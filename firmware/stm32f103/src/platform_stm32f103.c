#include "rcr_mcu/input_debounce.h"
#include "rcr_mcu/platform.h"
#include "rcr_mcu/servo_pwm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define RCC_BASE UINT32_C(0x40021000)
#define FLASH_BASE UINT32_C(0x40022000)
#define AFIO_BASE UINT32_C(0x40010000)
#define GPIOA_BASE UINT32_C(0x40010800)
#define GPIOC_BASE UINT32_C(0x40011000)
#define TIM1_BASE UINT32_C(0x40012C00)
#define CAN1_BASE UINT32_C(0x40006400)
#define IWDG_BASE UINT32_C(0x40003000)
#define SYSTICK_BASE UINT32_C(0xE000E010)
#define NVIC_ISER0 UINT32_C(0xE000E100)
#define SESSION_PAGE UINT32_C(0x0800FC00)

enum {
  QUEUE_CAPACITY = 8,
  SESSION_RECORD_COUNT = 256,
};

static volatile uint32_t monotonic_ms;
static rcr_can_frame_t rx_queue[QUEUE_CAPACITY];
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;
static volatile bool rx_overflow;
static rcr_can_frame_t tx_queue[QUEUE_CAPACITY];
static uint8_t tx_head;
static uint8_t tx_tail;

static void data_memory_barrier(void) {
  __asm volatile("dmb" ::: "memory");
}

static uint32_t enter_critical(void) {
  uint32_t primask;
  __asm volatile("mrs %0, primask\ncpsid i" : "=r"(primask) : : "memory");
  return primask;
}

static void leave_critical(uint32_t primask) {
  if ((primask & UINT32_C(1)) == 0U) {
    __asm volatile("cpsie i" ::: "memory");
  }
}

static void configure_led_off(void) {
  REG32(RCC_BASE + 0x18U) |= (UINT32_C(1) << 4U); /* IOPCEN */
  uint32_t crh = REG32(GPIOC_BASE + 0x04U);
  crh &= ~(UINT32_C(0xF) << 20U);
  crh |= (UINT32_C(0x2) << 20U); /* PC13 output push-pull, 2 MHz */
  REG32(GPIOC_BASE + 0x04U) = crh;
  REG32(GPIOC_BASE + 0x10U) = UINT32_C(1) << 13U; /* BSRR: high = LED off */
}

static bool configure_clock_72mhz(void) {
  REG32(RCC_BASE + 0x00U) |= UINT32_C(1) << 16U; /* HSEON */
  uint32_t timeout = UINT32_C(1000000);
  while ((REG32(RCC_BASE + 0x00U) & (UINT32_C(1) << 17U)) == 0U) {
    if (--timeout == 0U) {
      return false;
    }
  }

  REG32(FLASH_BASE + 0x00U) = (UINT32_C(1) << 4U) | UINT32_C(2);
  uint32_t cfgr = REG32(RCC_BASE + 0x04U);
  cfgr &= ~((UINT32_C(0xF) << 4U) | (UINT32_C(0x7) << 8U) |
            (UINT32_C(0x7) << 11U) | (UINT32_C(0xF) << 18U) |
            (UINT32_C(1) << 16U));
  cfgr |= (UINT32_C(0x4) << 8U);  /* APB1 = HCLK / 2 */
  cfgr |= UINT32_C(1) << 16U;     /* PLL source = HSE */
  cfgr |= UINT32_C(0x7) << 18U;   /* PLL x9 */
  REG32(RCC_BASE + 0x04U) = cfgr;

  REG32(RCC_BASE + 0x00U) |= UINT32_C(1) << 24U; /* PLLON */
  timeout = UINT32_C(1000000);
  while ((REG32(RCC_BASE + 0x00U) & (UINT32_C(1) << 25U)) == 0U) {
    if (--timeout == 0U) {
      return false;
    }
  }
  REG32(RCC_BASE + 0x04U) =
      (REG32(RCC_BASE + 0x04U) & ~UINT32_C(0x3)) | UINT32_C(0x2);
  timeout = UINT32_C(1000000);
  while ((REG32(RCC_BASE + 0x04U) & (UINT32_C(0x3) << 2U)) !=
         (UINT32_C(0x2) << 2U)) {
    if (--timeout == 0U) {
      return false;
    }
  }
  return true;
}

static void configure_systick(void) {
  REG32(SYSTICK_BASE + 0x00U) = 0U; /* CTRL: 配置完成前保持关闭 */
  REG32(SYSTICK_BASE + 0x04U) = UINT32_C(72000) - 1U; /* LOAD: 1 ms */
  REG32(SYSTICK_BASE + 0x08U) = 0U; /* VAL: 清当前计数和 COUNTFLAG */
  REG32(SYSTICK_BASE + 0x00U) = UINT32_C(7); /* core clock, IRQ, enable */
}

static void configure_target_sensor_input(void) {
  REG32(RCC_BASE + 0x18U) |= UINT32_C(1) << 2U; /* IOPAEN */
  /* PA0=TARGET_SENSOR_DO：不碰 PA8 TIM1、PA11/12 CAN、PA13/14 SWD。不配 EXTI。 */
  uint32_t crl = REG32(GPIOA_BASE + 0x00U);
  crl &= ~UINT32_C(0xF);
  crl |= UINT32_C(0x8); /* 输入，由 ODR 选择上拉/下拉 */
  REG32(GPIOA_BASE + 0x00U) = crl;
  /* 内部上拉是断线/开漏时的保守默认；极性仍等 bring-up，不在这里假设到位电平。 */
  REG32(GPIOA_BASE + 0x10U) = UINT32_C(1); /* BSRR: ODR0=1 → pull-up */
}

static void configure_servo_pwm_off(void) {
  REG32(RCC_BASE + 0x18U) |=
      (UINT32_C(1) << 2U) | (UINT32_C(1) << 11U); /* GPIOA + TIM1 */

  /* 在把 PA8 交给 TIM1 前先把 GPIO 输出锁定为低，避免初始化产生控制脉冲。 */
  REG32(GPIOA_BASE + 0x14U) = UINT32_C(1) << 8U;
  uint32_t crh = REG32(GPIOA_BASE + 0x04U);
  crh &= ~UINT32_C(0xF);
  crh |= UINT32_C(0xB); /* PA8 alternate-function push-pull, 50 MHz */
  REG32(GPIOA_BASE + 0x04U) = crh;

  REG32(TIM1_BASE + 0x00U) = 0U;                 /* CR1: stopped */
  REG32(TIM1_BASE + 0x28U) = UINT32_C(71);       /* PSC: 72MHz / 72 = 1MHz */
  REG32(TIM1_BASE + 0x2CU) = UINT32_C(19999);    /* ARR: 20ms / 50Hz */
  REG32(TIM1_BASE + 0x34U) = 0U;                 /* CCR1: no control pulse */
  REG32(TIM1_BASE + 0x18U) = UINT32_C(0x68);     /* PWM1 + CCR1 preload */
  REG32(TIM1_BASE + 0x20U) = UINT32_C(1);        /* CC1E, active high */
  REG32(TIM1_BASE + 0x44U) = UINT32_C(1) << 15U; /* BDTR.MOE */
  REG32(TIM1_BASE + 0x14U) = UINT32_C(1);        /* EGR.UG: load preload */
  REG32(TIM1_BASE + 0x00U) =
      (UINT32_C(1) << 7U) | UINT32_C(1); /* ARR preload + counter enable */
}

static bool configure_can(void) {
  REG32(RCC_BASE + 0x18U) |=
      (UINT32_C(1) << 0U) | (UINT32_C(1) << 2U); /* AFIO + GPIOA */
  REG32(RCC_BASE + 0x1CU) |= UINT32_C(1) << 25U; /* CAN1EN */

  uint32_t crh = REG32(GPIOA_BASE + 0x04U);
  crh &= ~((UINT32_C(0xF) << 12U) | (UINT32_C(0xF) << 16U));
  crh |= (UINT32_C(0x8) << 12U); /* PA11 input pull-up: CAN_RX */
  crh |= (UINT32_C(0xB) << 16U); /* PA12 AF push-pull 50 MHz: CAN_TX */
  REG32(GPIOA_BASE + 0x04U) = crh;
  REG32(GPIOA_BASE + 0x10U) = UINT32_C(1) << 11U;
  REG32(AFIO_BASE + 0x04U) &= ~(UINT32_C(0x3) << 13U); /* no CAN remap */

  REG32(CAN1_BASE + 0x00U) = (UINT32_C(1) << 6U) | UINT32_C(1); /* ABOM+INRQ */
  uint32_t timeout = UINT32_C(1000000);
  while ((REG32(CAN1_BASE + 0x04U) & UINT32_C(1)) == 0U) {
    if (--timeout == 0U) {
      return false;
    }
  }

  /* PCLK1=36MHz, BRP=4, BS1=15, BS2=2, SJW=1 => 500kbit/s, SP=88.9%. */
  REG32(CAN1_BASE + 0x1CU) =
      (UINT32_C(1) << 20U) | (UINT32_C(14) << 16U) | UINT32_C(3);

  REG32(CAN1_BASE + 0x200U) |= UINT32_C(1); /* filter init */
  REG32(CAN1_BASE + 0x21CU) &= ~UINT32_C(1);
  REG32(CAN1_BASE + 0x204U) &= ~UINT32_C(1); /* mask mode */
  REG32(CAN1_BASE + 0x20CU) |= UINT32_C(1);  /* 32-bit scale */
  REG32(CAN1_BASE + 0x214U) &= ~UINT32_C(1); /* FIFO0 */
  REG32(CAN1_BASE + 0x240U) = UINT32_C(0x061) << 21U;
  REG32(CAN1_BASE + 0x244U) =
      (UINT32_C(0x7FF) << 21U) | (UINT32_C(1) << 2U) |
      (UINT32_C(1) << 1U); /* exact standard data ID */
  REG32(CAN1_BASE + 0x21CU) |= UINT32_C(1);
  REG32(CAN1_BASE + 0x200U) &= ~UINT32_C(1);

  REG32(CAN1_BASE + 0x14U) = UINT32_C(1) << 1U; /* FIFO0 pending IRQ */
  REG32(NVIC_ISER0) = UINT32_C(1) << 20U;
  REG32(CAN1_BASE + 0x00U) &= ~UINT32_C(1);
  timeout = UINT32_C(1000000);
  while ((REG32(CAN1_BASE + 0x04U) & UINT32_C(1)) != 0U) {
    if (--timeout == 0U) {
      return false;
    }
  }
  return true;
}

bool rcr_platform_init(void) {
  configure_led_off();
  if (!configure_clock_72mhz()) {
    return false;
  }
  configure_target_sensor_input();
  configure_servo_pwm_off();
  configure_systick();
  return configure_can();
}

uint32_t rcr_platform_millis(void) { return monotonic_ms; }

void SysTick_Handler(void) { ++monotonic_ms; }

void USB_LP_CAN1_RX0_IRQHandler(void) {
  while ((REG32(CAN1_BASE + 0x0CU) & UINT32_C(0x3)) != 0U) {
    rcr_can_frame_t frame;
    const uint32_t rir = REG32(CAN1_BASE + 0x1B0U);
    const uint32_t rdtr = REG32(CAN1_BASE + 0x1B4U);
    const uint32_t rdlr = REG32(CAN1_BASE + 0x1B8U);
    const uint32_t rdhr = REG32(CAN1_BASE + 0x1BCU);
    frame.extended = (rir & (UINT32_C(1) << 2U)) != 0U;
    frame.rtr = (rir & (UINT32_C(1) << 1U)) != 0U;
    frame.can_id = frame.extended ? ((rir >> 3U) & UINT32_C(0x1FFFFFFF))
                                  : ((rir >> 21U) & UINT32_C(0x7FF));
    frame.dlc = (uint8_t)(rdtr & UINT32_C(0xF));
    frame.receive_ms = monotonic_ms;
    for (uint8_t index = 0U; index < 4U; ++index) {
      frame.data[index] = (uint8_t)(rdlr >> ((uint32_t)index * 8U));
      frame.data[index + 4U] = (uint8_t)(rdhr >> ((uint32_t)index * 8U));
    }

    const uint8_t next = (uint8_t)((rx_head + 1U) % QUEUE_CAPACITY);
    if (next == rx_tail) {
      rx_overflow = true;
    } else {
      rx_queue[rx_head] = frame;
      data_memory_barrier();
      rx_head = next;
    }
    REG32(CAN1_BASE + 0x0CU) = UINT32_C(1) << 5U; /* release FIFO0 */
  }
}

bool rcr_platform_can_rx_pop(rcr_can_frame_t *frame) {
  if (frame == NULL || rx_tail == rx_head) {
    return false;
  }
  data_memory_barrier();
  *frame = rx_queue[rx_tail];
  rx_tail = (uint8_t)((rx_tail + 1U) % QUEUE_CAPACITY);
  return true;
}

bool rcr_platform_take_rx_overflow(void) {
  const uint32_t primask = enter_critical();
  const bool overflow = rx_overflow;
  rx_overflow = false;
  leave_critical(primask);
  return overflow;
}

bool rcr_platform_can_tx_enqueue(const rcr_can_frame_t *frame) {
  if (frame == NULL || frame->extended || frame->rtr ||
      frame->dlc != RCR_CAN_V1_DLC) {
    return false;
  }
  const uint8_t next = (uint8_t)((tx_head + 1U) % QUEUE_CAPACITY);
  if (next == tx_tail) {
    return false;
  }
  tx_queue[tx_head] = *frame;
  tx_head = next;
  return true;
}

static bool try_load_tx_mailbox(const rcr_can_frame_t *frame) {
  const uint32_t tsr = REG32(CAN1_BASE + 0x08U);
  uint32_t mailbox_offset;
  if ((tsr & (UINT32_C(1) << 26U)) != 0U) {
    mailbox_offset = 0U;
  } else if ((tsr & (UINT32_C(1) << 27U)) != 0U) {
    mailbox_offset = 0x10U;
  } else if ((tsr & (UINT32_C(1) << 28U)) != 0U) {
    mailbox_offset = 0x20U;
  } else {
    return false;
  }

  const uint32_t low = (uint32_t)frame->data[0] |
                       ((uint32_t)frame->data[1] << 8U) |
                       ((uint32_t)frame->data[2] << 16U) |
                       ((uint32_t)frame->data[3] << 24U);
  const uint32_t high = (uint32_t)frame->data[4] |
                        ((uint32_t)frame->data[5] << 8U) |
                        ((uint32_t)frame->data[6] << 16U) |
                        ((uint32_t)frame->data[7] << 24U);
  const uint32_t base = CAN1_BASE + 0x180U + mailbox_offset;
  REG32(base + 0x00U) = (frame->can_id & UINT32_C(0x7FF)) << 21U;
  REG32(base + 0x04U) = RCR_CAN_V1_DLC;
  REG32(base + 0x08U) = low;
  REG32(base + 0x0CU) = high;
  REG32(base + 0x00U) |= UINT32_C(1); /* TXRQ 最后发布完整 mailbox */
  return true;
}

void rcr_platform_can_tx_pump(void) {
  while (tx_tail != tx_head) {
    if (!try_load_tx_mailbox(&tx_queue[tx_tail])) {
      return;
    }
    tx_tail = (uint8_t)((tx_tail + 1U) % QUEUE_CAPACITY);
  }
}

bool rcr_platform_can_bus_off(void) {
  return (REG32(CAN1_BASE + 0x18U) & (UINT32_C(1) << 2U)) != 0U;
}

bool rcr_platform_arbitration_probe_begin(void) {
  /* 诊断固件必须看到每一次失败尝试；NART 让仲裁丢失后 mailbox 立即完成而不自动重发。 */
  REG32(CAN1_BASE + 0x00U) |= UINT32_C(1); /* INRQ */
  uint32_t timeout = UINT32_C(1000000);
  while ((REG32(CAN1_BASE + 0x04U) & UINT32_C(1)) == 0U) {
    if (--timeout == 0U) {
      return false;
    }
  }
  REG32(CAN1_BASE + 0x00U) |= UINT32_C(1) << 4U; /* NART */
  REG32(CAN1_BASE + 0x00U) &= ~UINT32_C(1);
  timeout = UINT32_C(1000000);
  while ((REG32(CAN1_BASE + 0x04U) & UINT32_C(1)) != 0U) {
    if (--timeout == 0U) {
      return false;
    }
  }
  return (REG32(CAN1_BASE + 0x00U) & (UINT32_C(1) << 4U)) != 0U;
}

rcr_platform_probe_tx_result_t rcr_platform_arbitration_probe_poll(void) {
  const uint32_t tsr = REG32(CAN1_BASE + 0x08U);
  if ((tsr & UINT32_C(1)) == 0U) { /* RQCP0 */
    return RCR_PLATFORM_PROBE_TX_NONE;
  }

  rcr_platform_probe_tx_result_t result = RCR_PLATFORM_PROBE_TX_ERROR;
  if ((tsr & (UINT32_C(1) << 2U)) != 0U) { /* ALST0 */
    result = RCR_PLATFORM_PROBE_TX_ARBITRATION_LOST;
  } else if ((tsr & (UINT32_C(1) << 1U)) != 0U) { /* TXOK0 */
    result = RCR_PLATFORM_PROBE_TX_OK;
  }
  /* 写 1 清 RQCP0，同时清 TXOK0/ALST0/TERR0，下一次结果不会与本次混合。 */
  REG32(CAN1_BASE + 0x08U) = UINT32_C(1);
  return result;
}

bool rcr_platform_arbitration_probe_try_send(uint32_t sequence) {
  if ((REG32(CAN1_BASE + 0x08U) & (UINT32_C(1) << 26U)) == 0U) { /* TME0 */
    return false;
  }

  const uint32_t base = CAN1_BASE + 0x180U;
  REG32(base + 0x00U) = UINT32_C(0x7FE) << 21U;
  REG32(base + 0x04U) = RCR_CAN_V1_DLC;
  REG32(base + 0x08U) = sequence;
  REG32(base + 0x0CU) = UINT32_C(0xA55AA55A);
  REG32(base + 0x00U) |= UINT32_C(1); /* TXRQ 最后发布完整 mailbox */
  return true;
}

bool rcr_platform_target_sensor_raw_high(void) {
  return (REG32(GPIOA_BASE + 0x08U) & UINT32_C(1)) != 0U;
}

bool rcr_platform_target_sensor_active(void) {
  return rcr_target_sensor_active(
      rcr_platform_target_sensor_raw_high(),
      (rcr_target_sensor_polarity_t)RCR_TARGET_SENSOR_POLARITY);
}

void rcr_platform_set_output_led(bool enabled) {
  if (enabled) {
    REG32(GPIOC_BASE + 0x14U) = UINT32_C(1) << 13U; /* BRR: low = on */
  } else {
    REG32(GPIOC_BASE + 0x10U) = UINT32_C(1) << 13U; /* BSRR: high = off */
  }
}

void rcr_platform_set_servo_pwm_us(uint16_t pulse_us) {
  /* 只允许 SPEC 冻结的三种目标；意外参数不得形成任意舵机脉宽。 */
  if (pulse_us != RCR_SERVO_PWM_DISABLED_US &&
      pulse_us != RCR_SERVO_POSITION_A_US &&
      pulse_us != RCR_SERVO_POSITION_B_US) {
    pulse_us = RCR_SERVO_PWM_DISABLED_US;
  }
  /* CCR1 preload 在下一个 20ms update event 生效，不撕裂当前脉冲。 */
  REG32(TIM1_BASE + 0x34U) = pulse_us;
}

static bool flash_wait(void) {
  uint32_t timeout = UINT32_C(1000000);
  while ((REG32(FLASH_BASE + 0x0CU) & UINT32_C(1)) != 0U) {
    if (--timeout == 0U) {
      return false;
    }
  }
  return (REG32(FLASH_BASE + 0x0CU) &
          ((UINT32_C(1) << 2U) | (UINT32_C(1) << 4U))) == 0U;
}

static void flash_unlock(void) {
  if ((REG32(FLASH_BASE + 0x10U) & (UINT32_C(1) << 7U)) != 0U) {
    REG32(FLASH_BASE + 0x04U) = UINT32_C(0x45670123);
    REG32(FLASH_BASE + 0x04U) = UINT32_C(0xCDEF89AB);
  }
}

static void flash_lock(void) {
  REG32(FLASH_BASE + 0x10U) |= UINT32_C(1) << 7U;
}

static bool flash_program_halfword(uint32_t address, uint16_t value) {
  if (!flash_wait()) {
    return false;
  }
  REG32(FLASH_BASE + 0x10U) |= UINT32_C(1);
  *(volatile uint16_t *)(uintptr_t)address = value;
  const bool ok = flash_wait();
  REG32(FLASH_BASE + 0x10U) &= ~UINT32_C(1);
  return ok && *(volatile const uint16_t *)(uintptr_t)address == value;
}

static bool flash_erase_session_page(void) {
  if (!flash_wait()) {
    return false;
  }
  REG32(FLASH_BASE + 0x10U) |= UINT32_C(1) << 1U;
  REG32(FLASH_BASE + 0x14U) = SESSION_PAGE;
  REG32(FLASH_BASE + 0x10U) |= UINT32_C(1) << 6U;
  const bool ok = flash_wait();
  REG32(FLASH_BASE + 0x10U) &= ~(UINT32_C(1) << 1U);
  return ok && REG32(SESSION_PAGE) == UINT32_C(0xFFFFFFFF);
}

bool rcr_platform_next_session(uint16_t *session_id) {
  if (session_id == NULL) {
    return false;
  }
  uint16_t last = 0U;
  uint32_t empty_address = 0U;
  for (uint32_t index = 0U; index < SESSION_RECORD_COUNT; ++index) {
    const uint32_t address = SESSION_PAGE + index * UINT32_C(4);
    const uint32_t record = REG32(address);
    if (record == UINT32_C(0xFFFFFFFF)) {
      if (empty_address == 0U) {
        empty_address = address;
      }
      continue;
    }
    const uint16_t value = (uint16_t)(record & UINT32_C(0xFFFF));
    const uint16_t complement = (uint16_t)(record >> 16U);
    if (value != 0U && complement == (uint16_t)(~value)) {
      last = value;
    }
  }

  const uint16_t next =
      last == UINT16_C(0xFFFF) ? UINT16_C(1) : (uint16_t)(last + 1U);
  flash_unlock();
  if (empty_address == 0U) {
    if (!flash_erase_session_page()) {
      flash_lock();
      *session_id = next;
      return false;
    }
    empty_address = SESSION_PAGE;
  }
  const bool value_ok = flash_program_halfword(empty_address, next);
  const bool complement_ok =
      value_ok && flash_program_halfword(empty_address + UINT32_C(2),
                                         (uint16_t)(~next));
  flash_lock();
  *session_id = next;
  return complement_ok;
}

void rcr_platform_watchdog_start(void) {
  REG32(IWDG_BASE + 0x00U) = UINT32_C(0x5555);
  REG32(IWDG_BASE + 0x04U) = UINT32_C(3);    /* LSI / 32 */
  REG32(IWDG_BASE + 0x08U) = UINT32_C(1250); /* nominal ~1 s */
  REG32(IWDG_BASE + 0x00U) = UINT32_C(0xAAAA);
  REG32(IWDG_BASE + 0x00U) = UINT32_C(0xCCCC);
}

void rcr_platform_watchdog_kick(void) {
  REG32(IWDG_BASE + 0x00U) = UINT32_C(0xAAAA);
}
