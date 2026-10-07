#ifndef AT32_ODRIVE_CAN_COMM_H
#define AT32_ODRIVE_CAN_COMM_H

#include <stdbool.h>
#include <stdint.h>

void can_comm_init(void);
void can_comm_poll(void);
void can_comm_send_heartbeat(void);
void can_comm_send_encoder_estimates(void);

#endif /* AT32_ODRIVE_CAN_COMM_H */
