#include "ws2812.h"

#include "board.h"
#include "irq_priority.h"
#include "led_pattern.h"

#include <stddef.h>

/*
 * WS2812B-2020 on PB2 = TMR20_CH1 (GPIO_MUX2); see Inc/board.h, docs/pinout.md
 * and the timing documentation in Inc/led_pattern.h.
 *
 * Frame pipeline
 * --------------
 * TMR20's channel 1 output buffer (C1OBEN) is enabled, so a value written to
 * TMRx_C1DT only becomes the active compare value at an overflow event.  That
 * gives the DMA one full bit period of slack:
 *
 *   period n    : the pin is driven by the active C1DT = frame[n - 1]
 *   CNT == C1DT : channel 1 compare event -> DMA loads frame[n] into the buffer
 *   overflow    : buffer -> active C1DT, so period n + 1 sends frame[n]
 *
 * frame[0] is therefore preloaded by software together with a software overflow
 * event while the counter is stopped, and the DMA carries
 * frame[1 .. LED_WS2812_FRAME_ENTRIES - 1].  The last entry is the flush slot:
 * its transfer sets the DMA transfer-complete flag right after the final colour
 * bit falls, and the interrupt handler stops the timer, holding the data line
 * low for the reset/latch time (>= 50 us; in practice the scheduler period).
 */

#define WS2812_TMR               TMR20
#define WS2812_TMR_CHANNEL       TMR_SELECT_CHANNEL_1
#define WS2812_DMA_CHANNEL       DMA1_CHANNEL1
#define WS2812_DMAMUX_CHANNEL    DMA1MUX_CHANNEL1

/* The DMA carries frame[1] .. frame[LED_WS2812_FRAME_ENTRIES - 1]. */
#define WS2812_DMA_ENTRIES       (LED_WS2812_FRAME_ENTRIES - 1u)

static uint32_t s_frame[LED_WS2812_FRAME_ENTRIES];
static volatile bool s_busy;
static volatile uint32_t s_frames_started;
static volatile uint32_t s_frames_completed;

static void ws2812_dma_arm(void)
{
  /*
   * The DMA leaves the memory address register pointing past the end of the
   * buffer, so both the source address and the transfer count are reloaded for
   * every frame.  The other channel settings set up by ws2812_init() stay.
   */
  WS2812_DMA_CHANNEL->maddr = (uint32_t)&s_frame[1];
  dma_data_number_set(WS2812_DMA_CHANNEL, (uint16_t)WS2812_DMA_ENTRIES);
  dma_flag_clear(DMA1_GL1_FLAG);
  dma_channel_enable(WS2812_DMA_CHANNEL, TRUE);
}

void ws2812_init(void)
{
  gpio_init_type gpio_init_struct;
  tmr_output_config_type oc;
  dma_init_type dma_init_struct;
  uint32_t i;

  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_TMR20_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, TRUE);

  /* PB2 -> alternate function 2 = TMR20_CH1 (reference manual Table 6-2). */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = GPIO_PINS_2;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOB, &gpio_init_struct);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE2, GPIO_MUX_2);

  /* One bit period per PWM cycle, up-counting. */
  tmr_base_init(WS2812_TMR, LED_WS2812_PERIOD_RELOAD, 0u);
  tmr_cnt_dir_set(WS2812_TMR, TMR_COUNT_UP);
  tmr_clock_source_div_set(WS2812_TMR, TMR_CLOCK_DIV1);

  tmr_output_default_para_init(&oc);
  oc.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
  oc.oc_output_state = TRUE;
  oc.oc_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  oc.oc_idle_state = FALSE;
  tmr_output_channel_config(WS2812_TMR, WS2812_TMR_CHANNEL, &oc);
  tmr_output_channel_buffer_enable(WS2812_TMR, WS2812_TMR_CHANNEL, TRUE);
  tmr_channel_value_set(WS2812_TMR, WS2812_TMR_CHANNEL, 0u);

  /* TMR20 is an advanced-control timer: the pin only drives while MOE is set. */
  tmr_output_enable(WS2812_TMR, TRUE);

  /* DMA request on the channel 1 compare event (DRS = 0), not on overflow. */
  tmr_channel_dma_select(WS2812_TMR, TMR_DMA_REQUEST_BY_CHANNEL);
  tmr_dma_request_enable(WS2812_TMR, TMR_C1_DMA_REQUEST, TRUE);
  tmr_counter_enable(WS2812_TMR, FALSE);

  dma_default_para_init(&dma_init_struct);
  dma_init_struct.peripheral_base_addr = (uint32_t)&WS2812_TMR->c1dt;
  dma_init_struct.memory_base_addr = (uint32_t)&s_frame[1];
  dma_init_struct.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
  dma_init_struct.buffer_size = (uint16_t)WS2812_DMA_ENTRIES;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_WORD;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_WORD;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_HIGH;
  dma_init(WS2812_DMA_CHANNEL, &dma_init_struct);
  dma_flexible_config(DMA1, WS2812_DMAMUX_CHANNEL, DMAMUX_DMAREQ_ID_TMR20_CH1);

  for (i = 0u; i < LED_WS2812_FRAME_ENTRIES; i++)
  {
    s_frame[i] = 0u;
  }
  s_busy = false;

  dma_interrupt_enable(WS2812_DMA_CHANNEL, DMA_FDT_INT, TRUE);
  nvic_irq_enable(DMA1_Channel1_IRQn, IRQ_PRIORITY_STATUS_LED, 0);
}

bool ws2812_write(uint8_t r, uint8_t g, uint8_t b)
{
  if (s_busy)
  {
    return false;
  }

  led_ws2812_encode(r, g, b, s_frame);

  s_busy = true;
  s_frames_started++;

  /* Reload the frame from a known state: counter stopped, bit 0 active. */
  tmr_counter_enable(WS2812_TMR, FALSE);
  tmr_channel_value_set(WS2812_TMR, WS2812_TMR_CHANNEL, s_frame[0]);
  tmr_event_sw_trigger(WS2812_TMR, TMR_OVERFLOW_SWTRIG);
  tmr_flag_clear(WS2812_TMR, TMR_OVF_FLAG | TMR_C1_FLAG);

  ws2812_dma_arm();
  tmr_counter_enable(WS2812_TMR, TRUE);
  return true;
}

bool ws2812_busy(void)
{
  return s_busy;
}

uint32_t ws2812_frames_started(void)
{
  return s_frames_started;
}

uint32_t ws2812_frames_completed(void)
{
  return s_frames_completed;
}

/*
 * One transfer per bit; the last one (the flush slot) ends the frame.
 * The priority is the lowest of the system (see Inc/irq_priority.h): this
 * handler only parks the timer/DMA after the frame, so it must never preempt
 * the current loop or the communication interrupts.
 */
void DMA1_Channel1_IRQHandler(void)
{
  if (dma_flag_get(DMA1_FDT1_FLAG) == RESET)
  {
    return;
  }

  tmr_counter_enable(WS2812_TMR, FALSE);
  dma_channel_enable(WS2812_DMA_CHANNEL, FALSE);
  dma_flag_clear(DMA1_GL1_FLAG);
  s_frames_completed++;
  s_busy = false;
}
