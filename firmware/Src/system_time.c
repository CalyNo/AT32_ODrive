#include "system_time.h"

#include "at32f435_437.h"

extern unsigned int system_core_clock;

static volatile uint32_t s_tick_ms;
static uint32_t s_cycles_per_us;

void SysTick_Handler(void)
{
  s_tick_ms++;
}

void system_time_init(void)
{
  uint32_t core_clock = system_core_clock;

  if (core_clock == 0u)
  {
    core_clock = 8000000u;
  }

  s_cycles_per_us = core_clock / 1000000u;
  if (s_cycles_per_us == 0u)
  {
    s_cycles_per_us = 1u;
  }

  /* Enable DWT cycle counter for microsecond delays and timestamps. */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  s_tick_ms = 0u;
  SysTick_Config(core_clock / 1000u);
}

uint32_t system_millis(void)
{
  return s_tick_ms;
}

uint32_t system_micros(void)
{
  if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u)
  {
    return DWT->CYCCNT / s_cycles_per_us;
  }
  return s_tick_ms * 1000u;
}

void system_delay_ms(uint32_t ms)
{
  uint32_t start = s_tick_ms;

  while ((s_tick_ms - start) < ms)
  {
    __WFI();
  }
}

void system_delay_us(uint32_t us)
{
  system_delay_cycles(us * s_cycles_per_us);
}

void system_delay_cycles(uint32_t cycles)
{
  uint32_t start = DWT->CYCCNT;

  while ((DWT->CYCCNT - start) < cycles)
  {
  }
}
