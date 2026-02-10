/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_cdc_if.h
  * @version        : v1.0_Cube
  * @brief          : Header for usbd_cdc_if.c file.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/

#ifndef __USBD_CDC_IF_H__
#define __USBD_CDC_IF_H__

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc.h"
#include "stm32f4xx_hal.h"

/* USER CODE BEGIN INCLUDE */

/* USER CODE END INCLUDE */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @brief For Usb device.
  * @{
  */

/** @defgroup USBD_CDC_IF USBD_CDC_IF
  * @brief Usb VCP device module
  * @{
  */

/** @defgroup USBD_CDC_IF_Exported_Defines USBD_CDC_IF_Exported_Defines
  * @brief Defines.
  * @{
  */
/* Define size for the receive and transmit buffer over CDC */
#define APP_RX_DATA_SIZE  2048
#define APP_TX_DATA_SIZE  2048
/* USER CODE BEGIN EXPORTED_DEFINES */
#define USB_PROTO_MAGIC                   0x31544255UL /* "UBT1" */
#define USB_PROTO_VERSION                 1U
#define USB_PROTO_HEADER_SIZE             16U
#define USB_PROTO_MAX_PAYLOAD             512U
#define USB_PROTO_MAX_FRAME_SIZE          (USB_PROTO_HEADER_SIZE + USB_PROTO_MAX_PAYLOAD)

#define USB_PROTO_CMD_HELLO_REQ           0x01U
#define USB_PROTO_CMD_PING_REQ            0x02U
#define USB_PROTO_CMD_ECHO_REQ            0x03U
#define USB_PROTO_CMD_START_STREAM_REQ    0x04U
#define USB_PROTO_CMD_STOP_STREAM_REQ     0x05U
#define USB_PROTO_CMD_STATS_REQ           0x06U
#define USB_PROTO_CMD_INTEGRITY_REQ       0x07U
#define USB_PROTO_CMD_LED_REQ             0x08U
#define USB_PROTO_CMD_RESET_REQ           0x09U
#define USB_PROTO_CMD_LOGIC_SNAPSHOT_REQ  0x0AU

#define USB_PROTO_RSP_HELLO               0x81U
#define USB_PROTO_RSP_PING                0x82U
#define USB_PROTO_RSP_ECHO                0x83U
#define USB_PROTO_RSP_START_STREAM        0x84U
#define USB_PROTO_RSP_STOP_STREAM         0x85U
#define USB_PROTO_RSP_STATS               0x86U
#define USB_PROTO_RSP_INTEGRITY           0x87U
#define USB_PROTO_RSP_LED                 0x88U
#define USB_PROTO_RSP_RESET_ACK           0x89U
#define USB_PROTO_RSP_LOGIC_SNAPSHOT      0x8AU
#define USB_PROTO_RSP_STREAM_DATA         0xE0U
#define USB_PROTO_RSP_WARN                0xF0U
#define USB_PROTO_RSP_ERROR               0xFFU

/* USER CODE END EXPORTED_DEFINES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Exported_Types USBD_CDC_IF_Exported_Types
  * @brief Types.
  * @{
  */

/* USER CODE BEGIN EXPORTED_TYPES */
typedef struct
{
  uint32_t last_rx_ms;
  uint32_t last_tx_ms;
  uint32_t last_error_ms;
  uint32_t rx_ok;
  uint32_t rx_crc_fail;
  uint32_t rx_overflow;
  uint32_t tx_queue_overflow;
  uint32_t tx_busy_count;
  uint8_t stream_enabled;
  uint8_t usb_configured;
  uint8_t led_override_mode;
} CDC_AppDiag_t;

/* USER CODE END EXPORTED_TYPES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Exported_Macros USBD_CDC_IF_Exported_Macros
  * @brief Aliases.
  * @{
  */

/* USER CODE BEGIN EXPORTED_MACRO */

/* USER CODE END EXPORTED_MACRO */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Exported_Variables USBD_CDC_IF_Exported_Variables
  * @brief Public variables.
  * @{
  */

/** CDC Interface callback. */
extern USBD_CDC_ItfTypeDef USBD_Interface_fops_FS;

/* USER CODE BEGIN EXPORTED_VARIABLES */

/* USER CODE END EXPORTED_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Exported_FunctionsPrototype USBD_CDC_IF_Exported_FunctionsPrototype
  * @brief Public functions declaration.
  * @{
  */

uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len);

/* USER CODE BEGIN EXPORTED_FUNCTIONS */
void CDC_AppInit(CRC_HandleTypeDef *hcrc, DMA_HandleTypeDef *hdma_memcpy);
void CDC_AppTask(uint32_t now_ms);
void CDC_AppGetDiag(CDC_AppDiag_t *diag);
uint8_t CDC_AppGetLedOverrideMode(void);

/* USER CODE END EXPORTED_FUNCTIONS */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CDC_IF_H__ */

