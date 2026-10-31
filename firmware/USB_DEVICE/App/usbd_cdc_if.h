/**
  ******************************************************************************
  * @file    usbd_cdc_if.h
  * @brief   CDC interface glue between the ST USB middleware and usb_link.
  ******************************************************************************
  */
#ifndef __USBD_CDC_IF_H__
#define __USBD_CDC_IF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_cdc.h"
#include "stm32f4xx_hal.h"

/* Endpoint buffers: one full-speed bulk packet is 64 bytes, keep room for a few. */
#define APP_RX_DATA_SIZE  256
#define APP_TX_DATA_SIZE  64

extern USBD_CDC_ItfTypeDef USBD_Interface_fops_FS;

uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CDC_IF_H__ */
