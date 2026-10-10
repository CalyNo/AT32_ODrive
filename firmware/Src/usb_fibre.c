#include "usb_fibre.h"

#include "board.h"
#include "cdc_class.h"
#include "fibre_server.h"
#include "usb_core.h"

/*
 * The AT32 USB device library requires this exact global name; it is defined
 * once in Src/usb_cdc.c.  The Fibre transport shares the same USB device
 * instance and class handler.
 */
extern otg_core_type otg_core_struct;

static bool s_active;
static uint8_t s_rx_packet[USBD_FIBRE_OUT_MAXPACKET_SIZE];

void usb_fibre_init(void)
{
  if (board_clock_is_degraded())
  {
    s_active = false;
    return;
  }

  fibre_server_init();
  s_active = true;
}

void usb_fibre_poll(void)
{
  uint16_t length;

  if (!s_active)
  {
    return;
  }

  length = usb_vendor_get_rxdata(&otg_core_struct, s_rx_packet);
  if (length > 0u)
  {
    fibre_server_process_packet(s_rx_packet, length);
  }
}

bool usb_fibre_send(const uint8_t *data, uint16_t length)
{
  if (!s_active || (data == NULL) || (length == 0u))
  {
    return false;
  }

  return usb_vendor_send_data(&otg_core_struct,
                              (uint8_t *)data,
                              length) == SUCCESS;
}
