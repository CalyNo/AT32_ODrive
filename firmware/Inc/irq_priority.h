#ifndef AT32_ODRIVE_IRQ_PRIORITY_H
#define AT32_ODRIVE_IRQ_PRIORITY_H

/*
 * Single place where the interrupt preemption policy is defined.
 *
 * The core is configured with NVIC_PRIORITY_GROUP_4 (4 bits of preemption
 * priority, no sub-priority), so a lower number means a higher urgency.  The
 * ordering below is a safety requirement, not a tuning knob:
 *
 *   ADCs  - the current loop must never be delayed by communication or by the
 *           velocity loop, so it preempts everything else.
 *   COMM  - CAN/UART are short and must stay responsive.
 *   CTRL  - the 8 kHz velocity/position loop runs encoder SPI reads plus float
 *           control math, so it is the lowest priority of the three.
 *
 * Keep this list in sync with docs/architecture.md.
 */
#define IRQ_PRIORITY_CURRENT_LOOP   (1u)   /* ADC1_2_3_IRQn */
#define IRQ_PRIORITY_COMMUNICATION  (2u)   /* USART3_IRQn, CAN1_RX0_IRQn */
#define IRQ_PRIORITY_CONTROL_LOOP   (3u)   /* TMR2_GLOBAL_IRQn */

/* SysTick stays at the reset default (0) and only increments a counter. */

#endif /* AT32_ODRIVE_IRQ_PRIORITY_H */
