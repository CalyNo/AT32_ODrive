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

  /*
   * Pass &otg_core_struct.dev, NOT &otg_core_struct.  The AT32 CDC class casts
   * this void * argument straight to usbd_core_type * (cdc_class.c:429), and
   * otg_core_type's first member is the usb_reg pointer (Inc/usb_core.h:90-103),
   * so handing it the otg struct made class_handler resolve from the wrong
   * offset.  That looked harmless while the Fibre endpoint was idle -- the bogus
   * cdc_struct read as "no data" and the call returned early -- but the first
   * received byte drove the wrong buffer and the wrong udev into
   * usbd_ept_recv(), faulting the CPU.  With the IWDG armed that fault became a
   * 200 ms reset loop, which is what "sending any serial byte reboots the board"
   * was.  usb_cdc.c passes &otg_core_struct.dev to the matching usb_vcp_* calls
   * for the same reason.
   */
  length = usb_vendor_get_rxdata(&otg_core_struct.dev, s_rx_packet);
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

  /* Same argument contract as usb_vendor_get_rxdata() above: usbd_core_type *. */
  return usb_vendor_send_data(&otg_core_struct.dev,
                              (uint8_t *)data,
                              length) == SUCCESS;
}
