/**
 * @file    usb_link.c
 * @brief   Byte-stream transport over the USB CDC class.
 *
 * Rings use free-running 32-bit counters (size is a power of two), so a full
 * ring is distinguishable from an empty one without a wasted slot and each
 * counter has exactly one writer.
 */
#include "usb_link.h"

#include <string.h>

#include "usbd_cdc_if.h"

#define RX_RING_SIZE   2048U
#define TX_RING_SIZE   16384U
#define TX_MAX_XFER    4096U   /* one USB transfer; the core splits it in 64-byte packets */

_Static_assert((RX_RING_SIZE & (RX_RING_SIZE - 1U)) == 0U, "RX ring must be a power of two");
_Static_assert((TX_RING_SIZE & (TX_RING_SIZE - 1U)) == 0U, "TX ring must be a power of two");

extern USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t s_rx[RX_RING_SIZE];
static volatile uint32_t s_rx_head;   /* written by ISR   */
static volatile uint32_t s_rx_tail;   /* written by main  */

static uint8_t s_tx[TX_RING_SIZE];
static volatile uint32_t s_tx_head;   /* written by main  */
static volatile uint32_t s_tx_tail;   /* written by ISR (after a transfer completes) */
static volatile uint32_t s_tx_inflight;

void usb_link_reset(void)
{
  __disable_irq();
  s_rx_head = s_rx_tail = 0U;
  s_tx_head = s_tx_tail = 0U;
  s_tx_inflight = 0U;
  __enable_irq();
}

bool usb_link_connected(void)
{
  return hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED;
}

size_t usb_link_rx_available(void)
{
  return (size_t)(s_rx_head - s_rx_tail);
}

size_t usb_link_rx_peek(uint8_t *dst, size_t len)
{
  size_t avail = usb_link_rx_available();
  uint32_t tail = s_rx_tail;

  if (len > avail)
  {
    len = avail;
  }
  for (size_t i = 0U; i < len; i++)
  {
    dst[i] = s_rx[(tail + i) & (RX_RING_SIZE - 1U)];
  }
  return len;
}

void usb_link_rx_drop(size_t len)
{
  size_t avail = usb_link_rx_available();

  s_rx_tail += (uint32_t)((len > avail) ? avail : len);
}

void usb_link_on_rx(const uint8_t *data, uint32_t len)
{
  uint32_t head = s_rx_head;
  uint32_t room = RX_RING_SIZE - (head - s_rx_tail);

  /* Host flow control is not available on SUMP: on overflow the tail of the burst is lost. */
  if (len > room)
  {
    len = room;
  }
  for (uint32_t i = 0U; i < len; i++)
  {
    s_rx[(head + i) & (RX_RING_SIZE - 1U)] = data[i];
  }
  s_rx_head = head + len;
}

size_t usb_link_tx_pending(void)
{
  return (size_t)(s_tx_head - s_tx_tail);
}

size_t usb_link_tx_free(void)
{
  return TX_RING_SIZE - usb_link_tx_pending();
}

size_t usb_link_tx_write(const uint8_t *src, size_t len)
{
  uint32_t head = s_tx_head;
  size_t room = usb_link_tx_free();

  if (len > room)
  {
    len = room;
  }
  for (size_t i = 0U; i < len; i++)
  {
    s_tx[(head + i) & (TX_RING_SIZE - 1U)] = src[i];
  }
  s_tx_head = head + (uint32_t)len;
  return len;
}

void usb_link_tx_discard(void)
{
  __disable_irq();
  s_tx_head = s_tx_tail + s_tx_inflight;
  __enable_irq();
}

void usb_link_task(void)
{
  uint32_t tail;
  uint32_t len;

  if ((s_tx_inflight != 0U) || !usb_link_connected())
  {
    return;
  }

  tail = s_tx_tail;
  len = s_tx_head - tail;
  if (len == 0U)
  {
    return;
  }

  /* One contiguous segment per transfer. */
  if (len > (TX_RING_SIZE - (tail & (TX_RING_SIZE - 1U))))
  {
    len = TX_RING_SIZE - (tail & (TX_RING_SIZE - 1U));
  }
  if (len > TX_MAX_XFER)
  {
    len = TX_MAX_XFER;
  }

  s_tx_inflight = len;
  if (CDC_Transmit_FS(&s_tx[tail & (TX_RING_SIZE - 1U)], (uint16_t)len) != USBD_OK)
  {
    s_tx_inflight = 0U;   /* busy: retry on the next call */
  }
}

void usb_link_on_tx_done(void)
{
  s_tx_tail += s_tx_inflight;
  s_tx_inflight = 0U;
}
