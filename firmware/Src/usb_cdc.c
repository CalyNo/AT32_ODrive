#include "usb_cdc.h"

#include "board.h"
#include "config.h"
#include "irq_priority.h"
#include "system_time.h"
#include "uart_comm.h"

#include "usb_conf.h"
#include "usb_core.h"
#include "usbd_int.h"
#include "cdc_class.h"
#include "cdc_desc.h"

/*
 * USB FS CDC-ACM device on OTGFS1 (PA11 = DM, PA12 = DP).  See Inc/usb_cdc.h.
 *
 * Two implementation details come from the AT32 USB device library:
 *
 *  - the interrupt entry and the class handlers are looked up through the
 *    global `otg_core_struct`, so it must keep exactly this name and be defined
 *    once (the SDK's virtual_comport example does the same in main.c);
 *  - usbd_ept_send() keeps a *pointer* to the buffer and pushes it out over
 *    several interrupts, so the staging buffer below must not be touched until
 *    the class reports the transfer complete (g_tx_completed).
 */

/* Required by the USB device library (see the comment above). */
otg_core_type otg_core_struct;

#define USB_CDC_TX_RING_SIZE   (512u)
#define USB_CDC_TX_RING_MASK   (USB_CDC_TX_RING_SIZE - 1u)
#define USB_CDC_TX_CHUNK       (USBD_CDC_OUT_MAXPACKET_SIZE)

static uint8_t s_tx_ring[USB_CDC_TX_RING_SIZE];
static volatile uint16_t s_tx_head;
static volatile uint16_t s_tx_tail;

/* Owned by the USB stack between usb_vcp_send_data() returning SUCCESS and the
 * class setting g_tx_completed again. */
static uint8_t s_tx_stage[USB_CDC_TX_CHUNK];

static uint8_t s_rx_stage[USBD_CDC_OUT_MAXPACKET_SIZE];

static volatile bool s_active;

uint32_t g_usb_cdc_connected;
uint32_t g_usb_cdc_rx_bytes;
uint32_t g_usb_cdc_tx_dropped;

static cdc_struct_type *usb_cdc_data(void)
{
  return (cdc_struct_type *)otg_core_struct.dev.class_handler->pdata;
}

void usb_cdc_init(void)
{
  gpio_init_type gpio_init_struct;

  /*
   * OTGFS needs an exact 48 MHz.  This board runs 288 MHz, so the divider is 6.
   * The HICK fallback is ~+-3 % off and cannot enumerate, so USB stays off when
   * the 8 MHz oscillator did not start (board_clock_is_degraded()).
   */
  if (board_clock_is_degraded())
  {
    return;
  }

  crm_periph_clock_enable(OTG_CLOCK, TRUE);
  crm_usb_clock_div_set(CRM_USB_DIV_6);

  /*
   * PA11 = USB_DM, PA12 = USB_DP, alternate function 10.  VBUS sensing is
   * disabled in usb_conf.h (USB_VBUS_IGNORE) because its pin would be PA9,
   * which is PWM_B_H on this board.
   */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = OTG_PIN_DM | OTG_PIN_DP;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(OTG_PIN_GPIO, &gpio_init_struct);
  gpio_pin_mux_config(OTG_PIN_GPIO, OTG_PIN_DM_SOURCE, OTG_PIN_MUX);
  gpio_pin_mux_config(OTG_PIN_GPIO, OTG_PIN_DP_SOURCE, OTG_PIN_MUX);

  nvic_irq_enable(OTG_IRQ, IRQ_PRIORITY_USB, 0);

  (void)usbd_init(&otg_core_struct,
                  USB_FULL_SPEED_CORE_ID,
                  USB_ID,
                  &cdc_class_handler,
                  &cdc_desc_handler);

  s_active = true;
}

bool usb_cdc_connected(void)
{
  if (!s_active)
  {
    return false;
  }

  return usbd_connect_state_get(&otg_core_struct.dev) == USB_CONN_STATE_CONFIGURED;
}

void usb_cdc_write(const uint8_t *data, uint32_t len)
{
  uint32_t i;

  if (!s_active || (data == NULL))
  {
    return;
  }

  for (i = 0u; i < len; i++)
  {
    uint16_t next = (uint16_t)((s_tx_head + 1u) & USB_CDC_TX_RING_MASK);

    if (next == s_tx_tail)
    {
      /* Ring full: drop the rest instead of blocking the caller. */
      g_usb_cdc_tx_dropped++;
      return;
    }
    s_tx_ring[s_tx_head] = data[i];
    s_tx_head = next;
  }
}

void usb_cdc_poll(void)
{
  uint16_t count;
  uint16_t index;
  uint16_t i;

  if (!s_active)
  {
    g_usb_cdc_connected = 0u;
    return;
  }

  g_usb_cdc_connected = usb_cdc_connected() ? 1u : 0u;

  /* Host -> device: feed the shared ASCII parser. */
  count = usb_vcp_get_rxdata(&otg_core_struct.dev, s_rx_stage);
  if (count > (uint16_t)USBD_CDC_OUT_MAXPACKET_SIZE)
  {
    count = (uint16_t)USBD_CDC_OUT_MAXPACKET_SIZE;
  }
  for (i = 0u; i < count; i++)
  {
    uart_comm_feed(s_rx_stage[i]);
  }
  g_usb_cdc_rx_bytes += count;

  /* Device -> host: one packet per call, only while the stack is idle. */
  if (usb_cdc_data()->g_tx_completed == 0u)
  {
    return;
  }

  index = s_tx_tail;
  count = 0u;
  while ((count < USB_CDC_TX_CHUNK) && (index != s_tx_head))
  {
    s_tx_stage[count] = s_tx_ring[index];
    index = (uint16_t)((index + 1u) & USB_CDC_TX_RING_MASK);
    count++;
  }

  if (count == 0u)
  {
    return;
  }

  if (usb_vcp_send_data(&otg_core_struct.dev, s_tx_stage, count) == SUCCESS)
  {
    /* Accepted: the bytes are now the USB stack's, release them. */
    s_tx_tail = index;
  }
}

/*
 * Hook required by the AT32 USB device library: its (optional) remote-wakeup
 * path delays 10 ms there.  This project never calls that path, but Keil links
 * the whole object set (no section garbage collection), so the symbol must
 * exist or the image fails to link (L6218E).  DWT-based busy wait: independent
 * of interrupts and bounded by construction.
 */
void usb_delay_ms(uint32_t ms)
{
  system_delay_us(ms * 1000u);
}

/*
 * Hook required by the AT32 CDC class: the SDK example uses it to reconfigure
 * the bridged UART from the host's SET_LINE_CODING request.  This is a virtual
 * port, so the requested rate is nominal and USART3 keeps its own 115200 baud
 * (the UART and USB ports are independent hosts of the same protocol).
 */
void usb_usart_config(linecoding_type linecoding)
{
  (void)linecoding;
}

/*
 * USB FS interrupt: the library entry point drives enumeration, the CDC control
 * requests and the bulk endpoints.
 */
void OTGFS1_IRQHandler(void)
{
  usbd_irq_handler(&otg_core_struct);
}
