#include "scheduler.h"
#include "system_time.h"

#include <stddef.h>

#include "at32f435_437.h"

#define SCHEDULER_MAX_TASKS   (16u)

static scheduler_task_t s_tasks[SCHEDULER_MAX_TASKS];

void scheduler_init(void)
{
  for (uint32_t i = 0u; i < SCHEDULER_MAX_TASKS; i++)
  {
    s_tasks[i].fn = NULL;
    s_tasks[i].context = NULL;
    s_tasks[i].period_ms = 0u;
    s_tasks[i].last_run_ms = 0u;
    s_tasks[i].used = false;
  }
}

bool scheduler_add_task(scheduler_task_fn_t fn, void *context, uint32_t period_ms)
{
  if ((fn == NULL) || (period_ms == 0u))
  {
    return false;
  }

  for (uint32_t i = 0u; i < SCHEDULER_MAX_TASKS; i++)
  {
    if (!s_tasks[i].used)
    {
      s_tasks[i].fn = fn;
      s_tasks[i].context = context;
      s_tasks[i].period_ms = period_ms;
      s_tasks[i].last_run_ms = system_millis();
      s_tasks[i].used = true;
      return true;
    }
  }

  return false;
}

void scheduler_run_once(void)
{
  uint32_t now = system_millis();

  for (uint32_t i = 0u; i < SCHEDULER_MAX_TASKS; i++)
  {
    if (s_tasks[i].used)
    {
      if ((now - s_tasks[i].last_run_ms) >= s_tasks[i].period_ms)
      {
        s_tasks[i].last_run_ms = now;
        s_tasks[i].fn(s_tasks[i].context);
      }
    }
  }
}

void scheduler_run_forever(void)
{
  while (1)
  {
    scheduler_run_once();
    __WFI();
  }
}
