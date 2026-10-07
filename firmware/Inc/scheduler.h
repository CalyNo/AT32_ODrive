#ifndef AT32_ODRIVE_SCHEDULER_H
#define AT32_ODRIVE_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*scheduler_task_fn_t)(void *context);

typedef struct
{
  scheduler_task_fn_t fn;
  void *context;
  uint32_t period_ms;
  uint32_t last_run_ms;
  bool used;
} scheduler_task_t;

void scheduler_init(void);
bool scheduler_add_task(scheduler_task_fn_t fn, void *context, uint32_t period_ms);
void scheduler_run_once(void);
void scheduler_run_forever(void);

#endif /* AT32_ODRIVE_SCHEDULER_H */
