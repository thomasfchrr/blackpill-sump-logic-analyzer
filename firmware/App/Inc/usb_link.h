/**
 * @file    usb_link.h
 * @brief   Byte-stream transport over the USB CDC class (RX/TX ring buffers).
 *
 * The USB ISR only moves bytes between the CDC endpoint buffers and two
 * single-producer/single-consumer rings. All protocol logic runs in the main
 * loop and never blocks.
 */
#ifndef USB_LINK_H
#define USB_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void usb_link_reset(void);
bool usb_link_connected(void);

/* RX side (consumer = main loop). */
size_t usb_link_rx_available(void);
size_t usb_link_rx_peek(uint8_t *dst, size_t len);   /* copies, does not consume */
void   usb_link_rx_drop(size_t len);

/* TX side (producer = main loop). Returns the number of bytes accepted. */
size_t usb_link_tx_free(void);
size_t usb_link_tx_write(const uint8_t *src, size_t len);
size_t usb_link_tx_pending(void);
void   usb_link_tx_discard(void);                    /* drop queued (not in-flight) data */

/** Push queued TX data to the USB core. Call from the main loop. */
void usb_link_task(void);

/* Called from the CDC interface callbacks (ISR context). */
void usb_link_on_rx(const uint8_t *data, uint32_t len);
void usb_link_on_tx_done(void);

#endif /* USB_LINK_H */
