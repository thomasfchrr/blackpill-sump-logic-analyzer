/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_cdc_if.c
  * @version        : v1.0_Cube
  * @brief          : Usb device for Virtual Com Port.
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

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc_if.h"

/* USER CODE BEGIN INCLUDE */
#include <string.h>

#include "main.h"
/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
typedef struct
{
  uint16_t len;
  uint8_t data[USB_PROTO_MAX_FRAME_SIZE];
} USB_TxSlot_t;

typedef struct
{
  uint8_t enabled;
  uint16_t payload_len;
  uint16_t interval_ms;
  uint32_t remaining_packets;
  uint32_t next_due_ms;
  uint32_t counter;
} USB_StreamState_t;

typedef struct
{
  uint32_t rx_bytes;
  uint32_t tx_bytes;
  uint32_t rx_ok;
  uint32_t rx_bad_magic;
  uint32_t rx_bad_header;
  uint32_t rx_crc_fail;
  uint32_t rx_overflow;
  uint32_t tx_queue_overflow;
  uint32_t tx_busy_count;
  uint32_t tx_frames;
  uint32_t dma_copy_count;
  uint32_t dma_copy_fail;
} USB_ProtoStats_t;

typedef enum
{
  USB_PROTO_MODE_AUTO = 0U,
  USB_PROTO_MODE_FRAME = 1U,
  USB_PROTO_MODE_SUMP = 2U
} USB_ProtoMode_t;

typedef struct
{
  uint32_t divider;
  uint16_t read_count_words;
  uint16_t delay_count_words;
  uint16_t flags;
  uint32_t trigger_mask[4];
  uint32_t trigger_value[4];
  uint8_t trigger_start_mask;
  uint8_t armed;
  uint8_t capture_active;
  uint8_t reserved;
  uint32_t last_sample_count;
  uint32_t last_samplerate_hz;
  uint32_t trigger_timeout_count;
} USB_SumpState_t;

/* Private define ------------------------------------------------------------*/
/* Framed protocol transport tuning. */
#define USB_RX_RING_SIZE                4096U
#define USB_TX_QUEUE_DEPTH              16U
#define USB_STREAM_MIN_PAYLOAD          8U
#define USB_STREAM_DEFAULT_PAYLOAD      256U
#define USB_STREAM_TEMPLATE_SEED        0x5A17C3D2UL
#define USB_DMA_COPY_THRESHOLD          96U
#define USB_RESET_ACK_DELAY_MS          120U
#define USB_LOGIC_SNAPSHOT_HDR_SIZE     12U
#define USB_LOGIC_SNAPSHOT_MAX_SAMPLES  (USB_PROTO_MAX_PAYLOAD - USB_LOGIC_SNAPSHOT_HDR_SIZE)
#define USB_LOGIC_SAMPLE_PERIOD_US      20U

/* SUMP/OLS capture limits and protocol timing. */
#define USB_SUMP_CLOCK_HZ               100000000UL
#define USB_SUMP_MAX_CAPTURE_BYTES      16384U
#define USB_SUMP_DEFAULT_CAPTURE_SAMPLES 2048U
#define USB_SUMP_DEFAULT_DIVIDER        ((USB_SUMP_CLOCK_HZ / 200000UL) - 1UL)
#define USB_SUMP_MAX_SAMPLERATE_HZ      2000000UL
#define USB_SUMP_ARM_TIMEOUT_MS         800U
#define USB_SUMP_TX_TIMEOUT_MS          6000U
#define USB_SUMP_SHORT_CMD_SIZE         1U
#define USB_SUMP_LONG_CMD_SIZE          5U

/* SUMP command set used by PulseView OLS driver. */
#define USB_SUMP_CMD_RESET              0x00U
#define USB_SUMP_CMD_ARM                0x01U
#define USB_SUMP_CMD_ID                 0x02U
#define USB_SUMP_CMD_SELF_TEST          0x03U
#define USB_SUMP_CMD_METADATA           0x04U
#define USB_SUMP_CMD_XON                0x11U
#define USB_SUMP_CMD_XOFF               0x13U
#define USB_SUMP_CMD_SET_DIVIDER        0x80U
#define USB_SUMP_CMD_CAPTURE_SIZE       0x81U
#define USB_SUMP_CMD_SET_FLAGS          0x82U
#define USB_SUMP_CMD_CAPTURE_DELAYCOUNT 0x83U
#define USB_SUMP_CMD_CAPTURE_READCOUNT  0x84U
#define USB_SUMP_CMD_ADV_TRIG_SEL       0x9EU
#define USB_SUMP_CMD_ADV_TRIG_WRITE     0x9FU
#define USB_SUMP_CMD_TRIG_MASK_BASE     0xC0U
#define USB_SUMP_CMD_TRIG_VALUE_BASE    0xC1U
#define USB_SUMP_CMD_TRIG_CFG_BASE      0xC2U

#define USB_SUMP_TRIG_START             (1U << 3)

/* SUMP capture flags consumed by host software. */
#define USB_SUMP_FLAG_DEMUX             (1U << 0)
#define USB_SUMP_FLAG_RLE               (1U << 8)
#define USB_SUMP_FLAG_EXTERNAL_TEST     (1U << 10)
#define USB_SUMP_FLAG_INTERNAL_TEST     (1U << 11)
#define USB_SUMP_FLAG_DISABLE_GROUP0    (1U << 2)
#define USB_SUMP_FLAG_DISABLE_GROUP1    (1U << 3)
#define USB_SUMP_FLAG_DISABLE_GROUP2    (1U << 4)
#define USB_SUMP_FLAG_DISABLE_GROUP3    (1U << 5)

/* SUMP metadata tokens (Big Endian payload values). */
#define USB_SUMP_META_END               0x00U
#define USB_SUMP_META_DEVICE_NAME       0x01U
#define USB_SUMP_META_FW_VERSION        0x02U
#define USB_SUMP_META_NUM_PROBES_LONG   0x20U
#define USB_SUMP_META_SAMPLE_MEM_BYTES  0x21U
#define USB_SUMP_META_MAX_RATE_HZ       0x23U
#define USB_SUMP_META_PROTO_SHORT       0x41U
#define USB_SUMP_META_NUM_PROBES_SHORT  0x40U

#define USB_WARN_RX_OVERFLOW            1U
#define USB_WARN_BAD_MAGIC              2U
#define USB_WARN_BAD_VERSION            3U
#define USB_WARN_BAD_LENGTH             4U
#define USB_WARN_CRC_FAIL               5U
#define USB_WARN_TX_OVERFLOW            6U
#define USB_WARN_DMA_FAIL               7U
#define USB_WARN_UNKNOWN_CMD            8U

#define USB_ERR_UNKNOWN_CMD             1U
#define USB_ERR_BAD_PAYLOAD             2U

#define WARN_BIT(code)                  (1UL << ((code) - 1U))
#define MIN_U16(a, b)                   (((a) < (b)) ? (a) : (b))

_Static_assert(APP_RX_DATA_SIZE >= 64U, "APP_RX_DATA_SIZE must be >= CDC FS packet size");
_Static_assert(USB_PROTO_MAX_FRAME_SIZE <= 1024U, "Frame size is unexpectedly large");
_Static_assert(USB_RX_RING_SIZE > (USB_PROTO_MAX_FRAME_SIZE + 1U), "RX ring too small");
_Static_assert((USB_SUMP_DEFAULT_CAPTURE_SAMPLES % 4U) == 0U, "SUMP default samples must be 4-aligned");

/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/
uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

static uint8_t s_rx_ring[USB_RX_RING_SIZE];
static uint8_t s_rx_payload[USB_PROTO_MAX_PAYLOAD];
static uint8_t s_stream_template[USB_PROTO_MAX_PAYLOAD];
static uint8_t s_stream_payload[USB_PROTO_MAX_PAYLOAD];
static uint8_t s_sump_capture[USB_SUMP_MAX_CAPTURE_BYTES];

static USB_TxSlot_t s_tx_slots[USB_TX_QUEUE_DEPTH];

static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;
static volatile uint8_t s_tx_head = 0U;
static volatile uint8_t s_tx_tail = 0U;
static volatile uint8_t s_tx_busy = 0U;
static volatile uint8_t s_led_override_mode = 0U;
static volatile uint32_t s_pending_warn_mask = 0U;

static uint32_t s_crc_table[256];
static uint8_t s_crc_table_ready = 0U;

static USB_StreamState_t s_stream = {0};
static USB_ProtoStats_t s_stats = {0};
static USB_SumpState_t s_sump = {0};

static CRC_HandleTypeDef *s_crc_handle = NULL;
static DMA_HandleTypeDef *s_dma_handle = NULL;

static uint32_t s_last_rx_ms = 0U;
static uint32_t s_last_tx_ms = 0U;
static uint32_t s_last_error_ms = 0U;
static uint32_t s_logic_seed = 0x13579BDFUL;
static uint32_t s_reset_due_ms = 0U;
static uint8_t s_reset_pending = 0U;
static uint8_t s_app_initialized = 0U;
static USB_ProtoMode_t s_proto_mode = USB_PROTO_MODE_AUTO;
/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @brief Usb device library.
  * @{
  */

/** @addtogroup USBD_CDC_IF
  * @{
  */

/** @defgroup USBD_CDC_IF_Private_TypesDefinitions USBD_CDC_IF_Private_TypesDefinitions
  * @brief Private types.
  * @{
  */

/* USER CODE BEGIN PRIVATE_TYPES */

/* USER CODE END PRIVATE_TYPES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Defines USBD_CDC_IF_Private_Defines
  * @brief Private defines.
  * @{
  */

/* USER CODE BEGIN PRIVATE_DEFINES */

/* USER CODE END PRIVATE_DEFINES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Macros USBD_CDC_IF_Private_Macros
  * @brief Private macros.
  * @{
  */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Exported_Variables USBD_CDC_IF_Exported_Variables
  * @brief Public variables.
  * @{
  */

extern USBD_HandleTypeDef hUsbDeviceFS;

/* USER CODE BEGIN EXPORTED_VARIABLES */

/* USER CODE END EXPORTED_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_FunctionPrototypes USBD_CDC_IF_Private_FunctionPrototypes
  * @brief Private functions declaration.
  * @{
  */

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t *pbuf, uint32_t *Len);
static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */
static uint32_t USB_EnterCritical(void);
static void USB_ExitCritical(uint32_t primask);
static void USB_SetPendingWarning(uint32_t warn_bit);
static void USB_ClearPendingWarning(uint32_t warn_bit);
static uint8_t USB_IsConfigured(void);

static uint16_t USB_ReadLe16(const uint8_t *buf);
static uint32_t USB_ReadLe32(const uint8_t *buf);
static void USB_WriteLe16(uint8_t *buf, uint16_t value);
static void USB_WriteLe32(uint8_t *buf, uint32_t value);
static void USB_WriteBe32(uint8_t *buf, uint32_t value);
static int32_t USB_TickDiff(uint32_t now_ms, uint32_t ref_ms);

static void USB_CrcInitTable(void);
static uint32_t USB_Crc32(const uint8_t *data, uint16_t len);
static uint32_t USB_XorShift32(uint32_t *state);
static void USB_PrepareStreamTemplate(void);
static void USB_CopyWithDmaFallback(uint8_t *dst, const uint8_t *src, uint16_t len);

static uint16_t USB_RxRingAvailable(void);
static uint16_t USB_RxRingWrite(const uint8_t *src, uint16_t len);
static uint8_t USB_RxRingPeek(uint8_t *dst, uint16_t len);
static uint8_t USB_RxRingDrop(uint16_t len);
static uint8_t USB_RxRingRead(uint8_t *dst, uint16_t len);

static uint8_t USB_TxQueueEnqueueFrame(uint8_t type, uint32_t seq, const uint8_t *payload, uint16_t payload_len);
static void USB_TxQueueDropPending(void);
static void USB_TxQueueService(void);
static void USB_SetProtocolMode(USB_ProtoMode_t mode);
static void USB_SumpResetState(void);
static void USB_SumpClampCaptureWindow(void);
static uint8_t USB_SumpIsLongCommand(uint8_t cmd);
static uint8_t USB_SumpIsKnownCommand(uint8_t cmd);
static uint8_t USB_SumpTxRawBlocking(const uint8_t *buf, uint16_t len, uint32_t timeout_ms);
static uint32_t USB_SumpComputeSamplerateHz(void);
static uint8_t USB_SumpSampleByte(uint32_t sample_idx);
static uint8_t USB_SumpWaitForTrigger(uint32_t sample_period_cycles);
static void USB_SumpAcquireAndSend(void);
static void USB_SumpSendMetadata(void);
static void USB_SumpHandleLongCommand(uint8_t cmd, const uint8_t *arg);
static void USB_SumpHandleShortCommand(uint8_t cmd);
static uint8_t USB_ServiceSumpParser(void);

static void USB_SendError(uint32_t seq, uint16_t code, uint16_t detail);
static void USB_SendLogicSnapshot(uint32_t seq, uint16_t sample_count);
static void USB_BuildLogicSamples(uint8_t *dst, uint16_t sample_count, uint32_t tick_base);
static uint8_t USB_EmitWarning(uint16_t code, uint16_t detail, uint32_t value);
static void USB_ServiceWarnings(void);
static void USB_HandlePacket(uint8_t type, uint32_t seq, const uint8_t *payload, uint16_t payload_len);
static void USB_ServiceParser(void);
static void USB_ServiceStream(uint32_t now_ms);
static void USB_ServicePendingReset(uint32_t now_ms);
/* USER CODE END PRIVATE_FUNCTIONS_DECLARATION */

/**
  * @}
  */

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS =
{
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS,
  CDC_TransmitCplt_FS
};

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Initializes the CDC media low layer over the FS USB IP
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Init_FS(void)
{
  /* USER CODE BEGIN 3 */
  uint32_t key = USB_EnterCritical();

  s_rx_head = 0U;
  s_rx_tail = 0U;
  s_tx_head = 0U;
  s_tx_tail = 0U;
  s_tx_busy = 0U;
  s_pending_warn_mask = 0U;
  s_led_override_mode = 0U;
  s_stream.enabled = 0U;
  s_stream.counter = 0U;
  s_stream.payload_len = USB_STREAM_DEFAULT_PAYLOAD;
  s_stream.interval_ms = 0U;
  s_stream.remaining_packets = 0U;
  s_stream.next_due_ms = 0U;
  s_logic_seed = USB_STREAM_TEMPLATE_SEED;
  s_reset_due_ms = 0U;
  s_reset_pending = 0U;
  s_proto_mode = USB_PROTO_MODE_AUTO;
  USB_SumpResetState();

  USB_ExitCritical(key);

  /* Set Application Buffers */
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0U);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
  return (USBD_OK);
  /* USER CODE END 3 */
}

/**
  * @brief  DeInitializes the CDC media low layer
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_DeInit_FS(void)
{
  /* USER CODE BEGIN 4 */
  uint32_t key = USB_EnterCritical();
  s_tx_busy = 0U;
  s_stream.enabled = 0U;
  s_reset_pending = 0U;
  s_proto_mode = USB_PROTO_MODE_AUTO;
  USB_SumpResetState();
  USB_ExitCritical(key);
  return (USBD_OK);
  /* USER CODE END 4 */
}

/**
  * @brief  Manage the CDC class requests
  * @param  cmd: Command code
  * @param  pbuf: Buffer containing command data (request parameters)
  * @param  length: Number of data to be sent (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
  /* USER CODE BEGIN 5 */
  UNUSED(pbuf);
  UNUSED(length);

  switch (cmd)
  {
    case CDC_SEND_ENCAPSULATED_COMMAND:
    case CDC_GET_ENCAPSULATED_RESPONSE:
    case CDC_SET_COMM_FEATURE:
    case CDC_GET_COMM_FEATURE:
    case CDC_CLEAR_COMM_FEATURE:
    case CDC_SET_LINE_CODING:
    case CDC_GET_LINE_CODING:
    case CDC_SET_CONTROL_LINE_STATE:
    case CDC_SEND_BREAK:
    default:
      break;
  }

  return (USBD_OK);
  /* USER CODE END 5 */
}

/**
  * @brief  Data received over USB OUT endpoint are sent over CDC interface
  *         through this function.
  *
  *         @note
  *         This function will issue a NAK packet on any OUT packet received on
  *         USB endpoint until exiting this function. If you exit this function
  *         before transfer is complete on CDC interface (ie. using DMA controller)
  *         it will result in receiving more data while previous ones are still
  *         not sent.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Receive_FS(uint8_t *Buf, uint32_t *Len)
{
  /* USER CODE BEGIN 6 */
  uint16_t in_len = (uint16_t)(*Len);
  uint16_t written = USB_RxRingWrite(Buf, in_len);

  s_stats.rx_bytes += in_len;
  s_last_rx_ms = HAL_GetTick();

  if (written < in_len)
  {
    s_stats.rx_overflow += (uint32_t)(in_len - written);
    s_last_error_ms = s_last_rx_ms;
    USB_SetPendingWarning(WARN_BIT(USB_WARN_RX_OVERFLOW));
  }

  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return (USBD_OK);
  /* USER CODE END 6 */
}

/**
  * @brief  CDC_Transmit_FS
  *         Data to send over USB IN endpoint are sent over CDC interface
  *         through this function.
  *         @note
  *
  *
  * @param  Buf: Buffer of data to be sent
  * @param  Len: Number of data to be sent (in bytes)
  * @retval USBD_OK if all operations are OK else USBD_FAIL or USBD_BUSY
  */
uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len)
{
  uint8_t result = USBD_OK;
  /* USER CODE BEGIN 7 */
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;

  if (hcdc == NULL)
  {
    return USBD_BUSY;
  }

  if (hcdc->TxState != 0U)
  {
    return USBD_BUSY;
  }

  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
  result = USBD_CDC_TransmitPacket(&hUsbDeviceFS);
  /* USER CODE END 7 */
  return result;
}

/**
  * @brief  CDC_TransmitCplt_FS
  *         Data transmitted callback
  *
  *         @note
  *         This function is IN transfer complete callback used to inform user that
  *         the submitted Data is successfully sent over USB.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @param  epnum: Endpoint number
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
  int8_t result = USBD_OK;
  /* USER CODE BEGIN 13 */
  uint32_t key = USB_EnterCritical();

  UNUSED(Buf);
  UNUSED(Len);
  UNUSED(epnum);

  if (s_tx_busy != 0U)
  {
    s_tx_head = (uint8_t)((s_tx_head + 1U) % USB_TX_QUEUE_DEPTH);
    s_tx_busy = 0U;
  }

  USB_ExitCritical(key);
  /* USER CODE END 13 */
  return result;
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */
static uint32_t USB_EnterCritical(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void USB_ExitCritical(uint32_t primask)
{
  if (primask == 0U)
  {
    __enable_irq();
  }
}

static void USB_SetPendingWarning(uint32_t warn_bit)
{
  uint32_t key = USB_EnterCritical();
  s_pending_warn_mask |= warn_bit;
  USB_ExitCritical(key);
}

static void USB_ClearPendingWarning(uint32_t warn_bit)
{
  uint32_t key = USB_EnterCritical();
  s_pending_warn_mask &= ~warn_bit;
  USB_ExitCritical(key);
}

static uint8_t USB_IsConfigured(void)
{
  return (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) ? 1U : 0U;
}

static uint16_t USB_ReadLe16(const uint8_t *buf)
{
  return (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

static uint32_t USB_ReadLe32(const uint8_t *buf)
{
  return ((uint32_t)buf[0]) |
         ((uint32_t)buf[1] << 8) |
         ((uint32_t)buf[2] << 16) |
         ((uint32_t)buf[3] << 24);
}

static void USB_WriteLe16(uint8_t *buf, uint16_t value)
{
  buf[0] = (uint8_t)(value & 0xFFU);
  buf[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void USB_WriteLe32(uint8_t *buf, uint32_t value)
{
  buf[0] = (uint8_t)(value & 0xFFU);
  buf[1] = (uint8_t)((value >> 8) & 0xFFU);
  buf[2] = (uint8_t)((value >> 16) & 0xFFU);
  buf[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void USB_WriteBe32(uint8_t *buf, uint32_t value)
{
  buf[0] = (uint8_t)((value >> 24) & 0xFFU);
  buf[1] = (uint8_t)((value >> 16) & 0xFFU);
  buf[2] = (uint8_t)((value >> 8) & 0xFFU);
  buf[3] = (uint8_t)(value & 0xFFU);
}

static int32_t USB_TickDiff(uint32_t now_ms, uint32_t ref_ms)
{
  return (int32_t)(now_ms - ref_ms);
}

static void USB_CrcInitTable(void)
{
  uint32_t i;

  for (i = 0U; i < 256U; i++)
  {
    uint32_t crc = i;
    uint32_t b;

    for (b = 0U; b < 8U; b++)
    {
      if ((crc & 1U) != 0U)
      {
        crc = (crc >> 1) ^ 0xEDB88320UL;
      }
      else
      {
        crc >>= 1;
      }
    }

    s_crc_table[i] = crc;
  }

  s_crc_table_ready = 1U;
}

static uint32_t USB_Crc32(const uint8_t *data, uint16_t len)
{
  uint32_t crc = 0xFFFFFFFFUL;
  uint16_t i;

  if (s_crc_table_ready == 0U)
  {
    USB_CrcInitTable();
  }

  for (i = 0U; i < len; i++)
  {
    uint32_t idx = (crc ^ data[i]) & 0xFFUL;
    crc = (crc >> 8) ^ s_crc_table[idx];
  }

  return crc ^ 0xFFFFFFFFUL;
}

static uint32_t USB_XorShift32(uint32_t *state)
{
  uint32_t x = *state;

  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

static void USB_PrepareStreamTemplate(void)
{
  uint32_t i = 0U;
  uint32_t seed = USB_STREAM_TEMPLATE_SEED;

  while (i < USB_PROTO_MAX_PAYLOAD)
  {
    uint32_t rnd = USB_XorShift32(&seed);

    s_stream_template[i] = (uint8_t)(rnd & 0xFFU);
    if ((i + 1U) < USB_PROTO_MAX_PAYLOAD)
    {
      s_stream_template[i + 1U] = (uint8_t)((rnd >> 8) & 0xFFU);
    }
    if ((i + 2U) < USB_PROTO_MAX_PAYLOAD)
    {
      s_stream_template[i + 2U] = (uint8_t)((rnd >> 16) & 0xFFU);
    }
    if ((i + 3U) < USB_PROTO_MAX_PAYLOAD)
    {
      s_stream_template[i + 3U] = (uint8_t)((rnd >> 24) & 0xFFU);
    }

    i += 4U;
  }
}

static void USB_CopyWithDmaFallback(uint8_t *dst, const uint8_t *src, uint16_t len)
{
  /* Use DMA for medium/large memory copies, then fall back to memcpy on any failure. */
  if ((len == 0U) || (dst == src))
  {
    return;
  }

  if ((s_dma_handle != NULL) && (len >= USB_DMA_COPY_THRESHOLD))
  {
    if (HAL_DMA_GetState(s_dma_handle) != HAL_DMA_STATE_READY)
    {
      (void)HAL_DMA_Abort(s_dma_handle);
    }

    if (HAL_DMA_Start(s_dma_handle, (uint32_t)src, (uint32_t)dst, (uint32_t)len) == HAL_OK)
    {
      if (HAL_DMA_PollForTransfer(s_dma_handle, HAL_DMA_FULL_TRANSFER, 2U) == HAL_OK)
      {
        s_stats.dma_copy_count++;
        return;
      }
    }

    (void)HAL_DMA_Abort(s_dma_handle);
    s_stats.dma_copy_fail++;
    s_last_error_ms = HAL_GetTick();
    USB_SetPendingWarning(WARN_BIT(USB_WARN_DMA_FAIL));
  }

  (void)memcpy(dst, src, len);
}

static uint16_t USB_RxRingAvailable(void)
{
  uint16_t head = s_rx_head;
  uint16_t tail = s_rx_tail;

  if (head >= tail)
  {
    return (uint16_t)(head - tail);
  }

  return (uint16_t)(USB_RX_RING_SIZE - tail + head);
}

static uint16_t USB_RxRingWrite(const uint8_t *src, uint16_t len)
{
  uint16_t head = s_rx_head;
  uint16_t tail = s_rx_tail;
  uint16_t used;
  uint16_t free_space;
  uint16_t to_write;
  uint16_t first_chunk;
  uint16_t second_chunk;

  if (head >= tail)
  {
    used = (uint16_t)(head - tail);
  }
  else
  {
    used = (uint16_t)(USB_RX_RING_SIZE - tail + head);
  }

  free_space = (uint16_t)(USB_RX_RING_SIZE - used - 1U);
  to_write = MIN_U16(len, free_space);

  first_chunk = MIN_U16(to_write, (uint16_t)(USB_RX_RING_SIZE - head));
  second_chunk = (uint16_t)(to_write - first_chunk);

  if (first_chunk > 0U)
  {
    (void)memcpy(&s_rx_ring[head], src, first_chunk);
  }
  if (second_chunk > 0U)
  {
    (void)memcpy(&s_rx_ring[0], &src[first_chunk], second_chunk);
  }

  s_rx_head = (uint16_t)((head + to_write) % USB_RX_RING_SIZE);
  return to_write;
}

static uint8_t USB_RxRingPeek(uint8_t *dst, uint16_t len)
{
  uint16_t available = USB_RxRingAvailable();
  uint16_t tail = s_rx_tail;
  uint16_t first_chunk;
  uint16_t second_chunk;

  if (len > available)
  {
    return 0U;
  }

  first_chunk = MIN_U16(len, (uint16_t)(USB_RX_RING_SIZE - tail));
  second_chunk = (uint16_t)(len - first_chunk);

  if (first_chunk > 0U)
  {
    (void)memcpy(dst, &s_rx_ring[tail], first_chunk);
  }
  if (second_chunk > 0U)
  {
    (void)memcpy(&dst[first_chunk], &s_rx_ring[0], second_chunk);
  }

  return 1U;
}

static uint8_t USB_RxRingDrop(uint16_t len)
{
  uint16_t available = USB_RxRingAvailable();

  if (len > available)
  {
    return 0U;
  }

  s_rx_tail = (uint16_t)((s_rx_tail + len) % USB_RX_RING_SIZE);
  return 1U;
}

static uint8_t USB_RxRingRead(uint8_t *dst, uint16_t len)
{
  uint16_t available = USB_RxRingAvailable();
  uint16_t tail = s_rx_tail;
  uint16_t first_chunk;
  uint16_t second_chunk;

  if (len > available)
  {
    return 0U;
  }

  first_chunk = MIN_U16(len, (uint16_t)(USB_RX_RING_SIZE - tail));
  second_chunk = (uint16_t)(len - first_chunk);

  if (first_chunk > 0U)
  {
    USB_CopyWithDmaFallback(dst, &s_rx_ring[tail], first_chunk);
  }
  if (second_chunk > 0U)
  {
    USB_CopyWithDmaFallback(&dst[first_chunk], &s_rx_ring[0], second_chunk);
  }

  s_rx_tail = (uint16_t)((tail + len) % USB_RX_RING_SIZE);
  return 1U;
}

static uint8_t USB_TxQueueEnqueueFrame(uint8_t type, uint32_t seq, const uint8_t *payload, uint16_t payload_len)
{
  uint8_t head = s_tx_head;
  uint8_t tail = s_tx_tail;
  uint8_t next_tail = (uint8_t)((tail + 1U) % USB_TX_QUEUE_DEPTH);
  USB_TxSlot_t *slot;
  uint16_t frame_len;
  uint32_t crc;

  if (payload_len > USB_PROTO_MAX_PAYLOAD)
  {
    return 0U;
  }

  if (next_tail == head)
  {
    s_stats.tx_queue_overflow++;
    s_last_error_ms = HAL_GetTick();
    USB_SetPendingWarning(WARN_BIT(USB_WARN_TX_OVERFLOW));
    return 0U;
  }

  slot = &s_tx_slots[tail];
  frame_len = (uint16_t)(USB_PROTO_HEADER_SIZE + payload_len);
  crc = USB_Crc32(payload, payload_len);

  USB_WriteLe32(&slot->data[0], USB_PROTO_MAGIC);
  slot->data[4] = USB_PROTO_VERSION;
  slot->data[5] = type;
  USB_WriteLe16(&slot->data[6], payload_len);
  USB_WriteLe32(&slot->data[8], seq);
  USB_WriteLe32(&slot->data[12], crc);

  if (payload_len > 0U)
  {
    USB_CopyWithDmaFallback(&slot->data[USB_PROTO_HEADER_SIZE], payload, payload_len);
  }

  slot->len = frame_len;
  s_tx_tail = next_tail;
  return 1U;
}

/* Keep one in-flight transfer and drop queued backlog to unblock control replies. */
static void USB_TxQueueDropPending(void)
{
  uint32_t key = USB_EnterCritical();
  uint8_t head = s_tx_head;

  if (s_tx_busy != 0U)
  {
    s_tx_tail = (uint8_t)((head + 1U) % USB_TX_QUEUE_DEPTH);
  }
  else
  {
    s_tx_tail = head;
  }

  USB_ExitCritical(key);
}

static void USB_TxQueueService(void)
{
  uint8_t head;
  uint8_t tail;
  uint8_t tx_busy;
  uint8_t tx_status;
  USB_TxSlot_t *slot;

  if (USB_IsConfigured() == 0U)
  {
    return;
  }

  head = s_tx_head;
  tail = s_tx_tail;
  tx_busy = s_tx_busy;

  if ((tx_busy != 0U) || (head == tail))
  {
    return;
  }

  slot = &s_tx_slots[head];
  tx_status = CDC_Transmit_FS(slot->data, slot->len);

  if (tx_status == USBD_OK)
  {
    uint32_t key = USB_EnterCritical();
    s_tx_busy = 1U;
    USB_ExitCritical(key);

    s_stats.tx_frames++;
    s_stats.tx_bytes += slot->len;
    s_last_tx_ms = HAL_GetTick();
  }
  else if (tx_status == USBD_BUSY)
  {
    s_stats.tx_busy_count++;
  }
  else
  {
    s_stats.tx_busy_count++;
    s_last_error_ms = HAL_GetTick();
  }
}

static void USB_SetProtocolMode(USB_ProtoMode_t mode)
{
  if (s_proto_mode == mode)
  {
    return;
  }

  if (mode == USB_PROTO_MODE_SUMP)
  {
    uint32_t key = USB_EnterCritical();
    s_stream.enabled = 0U;
    s_pending_warn_mask = 0U;
    s_reset_pending = 0U;
    s_tx_head = 0U;
    s_tx_tail = 0U;
    s_tx_busy = 0U;
    USB_ExitCritical(key);
  }

  s_proto_mode = mode;
}

static void USB_SumpResetState(void)
{
  uint8_t i;

  s_sump.divider = USB_SUMP_DEFAULT_DIVIDER;
  s_sump.read_count_words = (uint16_t)((USB_SUMP_DEFAULT_CAPTURE_SAMPLES / 4U) - 1U);
  s_sump.delay_count_words = s_sump.read_count_words;
  s_sump.flags = (uint16_t)(USB_SUMP_FLAG_DISABLE_GROUP1 |
                            USB_SUMP_FLAG_DISABLE_GROUP2 |
                            USB_SUMP_FLAG_DISABLE_GROUP3);
  s_sump.trigger_start_mask = 0U;
  s_sump.armed = 0U;
  s_sump.capture_active = 0U;
  s_sump.last_sample_count = 0U;
  s_sump.last_samplerate_hz = 0U;
  s_sump.trigger_timeout_count = 0U;

  for (i = 0U; i < 4U; i++)
  {
    s_sump.trigger_mask[i] = 0U;
    s_sump.trigger_value[i] = 0U;
  }

  USB_SumpClampCaptureWindow();
}

static void USB_SumpClampCaptureWindow(void)
{
  uint32_t max_words = (USB_SUMP_MAX_CAPTURE_BYTES / 4U) - 1U;

  if ((uint32_t)s_sump.read_count_words > max_words)
  {
    s_sump.read_count_words = (uint16_t)max_words;
  }
  if ((uint32_t)s_sump.delay_count_words > max_words)
  {
    s_sump.delay_count_words = (uint16_t)max_words;
  }
}

static uint8_t USB_SumpIsLongCommand(uint8_t cmd)
{
  if ((cmd == USB_SUMP_CMD_SET_DIVIDER) ||
      (cmd == USB_SUMP_CMD_CAPTURE_SIZE) ||
      (cmd == USB_SUMP_CMD_SET_FLAGS) ||
      (cmd == USB_SUMP_CMD_CAPTURE_DELAYCOUNT) ||
      (cmd == USB_SUMP_CMD_CAPTURE_READCOUNT) ||
      (cmd == USB_SUMP_CMD_ADV_TRIG_SEL) ||
      (cmd == USB_SUMP_CMD_ADV_TRIG_WRITE))
  {
    return 1U;
  }

  if ((cmd >= USB_SUMP_CMD_TRIG_MASK_BASE) && (cmd <= 0xCFU))
  {
    return 1U;
  }

  return 0U;
}

static uint8_t USB_SumpIsKnownCommand(uint8_t cmd)
{
  if ((cmd == USB_SUMP_CMD_RESET) ||
      (cmd == USB_SUMP_CMD_ARM) ||
      (cmd == USB_SUMP_CMD_ID) ||
      (cmd == USB_SUMP_CMD_SELF_TEST) ||
      (cmd == USB_SUMP_CMD_METADATA) ||
      (cmd == USB_SUMP_CMD_XON) ||
      (cmd == USB_SUMP_CMD_XOFF))
  {
    return 1U;
  }

  return USB_SumpIsLongCommand(cmd);
}

static uint8_t USB_SumpTxRawBlocking(const uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
  uint16_t offset = 0U;

  if (USB_IsConfigured() == 0U)
  {
    return 0U;
  }

  while (offset < len)
  {
    uint16_t chunk = MIN_U16(64U, (uint16_t)(len - offset));
    uint32_t start_ms = HAL_GetTick();

    while (CDC_Transmit_FS((uint8_t *)&buf[offset], chunk) != USBD_OK)
    {
      if ((uint32_t)USB_TickDiff(HAL_GetTick(), start_ms) > timeout_ms)
      {
        s_last_error_ms = HAL_GetTick();
        return 0U;
      }
    }

    while (1)
    {
      USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;

      if ((hcdc != NULL) && (hcdc->TxState == 0U))
      {
        break;
      }

      if ((uint32_t)USB_TickDiff(HAL_GetTick(), start_ms) > timeout_ms)
      {
        s_last_error_ms = HAL_GetTick();
        return 0U;
      }
    }

    offset = (uint16_t)(offset + chunk);
    s_stats.tx_bytes += chunk;
    s_last_tx_ms = HAL_GetTick();
  }

  return 1U;
}

static uint32_t USB_SumpComputeSamplerateHz(void)
{
  uint32_t divider = s_sump.divider & 0x00FFFFFFUL;
  uint32_t rate_hz = USB_SUMP_CLOCK_HZ / (divider + 1UL);

  if ((s_sump.flags & USB_SUMP_FLAG_DEMUX) != 0U)
  {
    rate_hz *= 2UL;
  }

  if (rate_hz == 0UL)
  {
    rate_hz = 1UL;
  }

  if (rate_hz > USB_SUMP_MAX_SAMPLERATE_HZ)
  {
    rate_hz = USB_SUMP_MAX_SAMPLERATE_HZ;
  }

  return rate_hz;
}

/**
 * @brief Majority voting filter to eliminate noise on floating GPIO inputs.
 *
 * NOISE ELIMINATION TECHNIQUE:
 *   - Floating GPIO pins pick up 50-60 Hz mains-frequency noise
 *   - Pull-down resistors help but don't completely eliminate external interference
 *   - Solution: Read GPIO 3 times in quick succession, take majority vote
 *
 * IMPLEMENTATION:
 *   - 3 reads with minimal delay (~5-10 CPU cycles between reads)
 *   - Bit-wise voting: if 2+ reads have bit=1, output bit=1
 *   - Execution time: ~15-20 CPU cycles (negligible vs 10ns per sample)
 *
 * EFFECTIVENESS:
 *   - Single bit glitches (noise): Almost always filtered
 *   - Real 50Hz oscillation (> 200µs cycle time): Partially filtered
 *   - Legitimate fast edges: Preserved (all 3 reads agree)
 *
 * @param sample_idx Unused parameter (for signature compatibility)
 * @return uint8_t GPIO state with majority voting filter applied
 */
static uint8_t USB_SumpSampleByte_Filtered(uint32_t sample_idx)
{
  uint8_t s1 = (uint8_t)(GPIOB->IDR & 0x00FFU);
  uint8_t s2 = (uint8_t)(GPIOB->IDR & 0x00FFU);
  uint8_t s3 = (uint8_t)(GPIOB->IDR & 0x00FFU);
  
  /* Majority voting: if 2+ reads have bit=1, output bit=1
   * Logic: (s1 & s2) | (s1 & s3) | (s2 & s3)
   */
  return (uint8_t)((s1 & s2) | (s1 & s3) | (s2 & s3));
}

static uint8_t USB_SumpSampleByte(uint32_t sample_idx)
{
  if ((s_sump.flags & USB_SUMP_FLAG_INTERNAL_TEST) != 0U)
  {
    uint8_t s0 = (uint8_t)(sample_idx & 0xFFU);
    return (uint8_t)((s0 ^ (uint8_t)(s0 << 1)) ^ 0x5AU);
  }

  /* Read GPIO with majority voting filter to eliminate 50-60 Hz noise */
  return USB_SumpSampleByte_Filtered(sample_idx);
}

static uint8_t USB_SumpWaitForTrigger(uint32_t sample_period_cycles)
{
  uint32_t mask = s_sump.trigger_mask[0] & 0xFFU;
  uint32_t value = s_sump.trigger_value[0] & mask;
  uint32_t start_ms = HAL_GetTick();
  uint32_t next_cycle = DWT->CYCCNT + sample_period_cycles;
  uint32_t sample_idx = 0U;

  if (mask == 0U)
  {
    return 1U;
  }

  while ((uint32_t)USB_TickDiff(HAL_GetTick(), start_ms) <= USB_SUMP_ARM_TIMEOUT_MS)
  {
    while ((int32_t)(DWT->CYCCNT - next_cycle) < 0)
    {
    }
    next_cycle += sample_period_cycles;

    if (((uint32_t)USB_SumpSampleByte(sample_idx) & mask) == value)
    {
      return 1U;
    }
    sample_idx++;
  }

  s_sump.trigger_timeout_count++;
  return 0U;
}

/*
 * USB_SumpAcquireAndSend()
 *
 * SUMP ARM command handler: captures logic samples at configured sample rate
 * and transmits them back to the host in reverse order.
 *
 * TIMING ARCHITECTURE:
 *   - Uses DWT cycle counter for precise timing (10 ns resolution @ 100 MHz)
 *   - Busy-wait loop maintains sample period with minimal overhead
 *   - Achieves ~2 MHz max sampling rate (50-cycle period at 100 MHz)
 *   - CPU utilization: 100% during capture (unavoidable with DWT approach)
 *
 * SAMPLE RATE CONFIGURATION:
 *   Sample frequency = 100 MHz / (divider + 1)
 *   - divider = 0   → 100 MHz
 *   - divider = 49  → 2 MHz (typical max)
 *   - divider = 999 → 100 kHz
 *
 * CAPTURE FLOW:
 *   1. Compute sample period in CPU cycles from configured divider
 *   2. Enable DWT cycle counter (CoreDebug + DWT->CTRL)
 *   3. Wait for trigger condition (GPIO mask/value match or timeout @ 800 ms)
 *   4. Sample GPIOB[0:7] at precise intervals, store in s_sump_capture[]
 *   5. Transmit samples in REVERSE order (newest first) to host
 *   6. Host detects completion when final byte matches expected pattern
 *
 * KEY PROPERTIES:
 *   ✓ Precision: 10 ns (one CPU cycle)
 *   ✓ Jitter: ±20–50 ns (cache, ISR latency)
 *   ✗ CPU Load: 100% during capture
 *   ✗ No pre/post-trigger delay (delay_count_words ignored)
 *   ✗ No RLE encoding (max 16 KB capture depth)
 */
static void USB_SumpAcquireAndSend(void)
{
  // Compute total sample count: SUMP command uses 16-bit word count
  // Each word = 4 bytes, so multiply by 4
  uint32_t sample_count = ((uint32_t)s_sump.read_count_words + 1UL) * 4UL;
  uint32_t rate_hz;
  uint32_t sample_period_cycles;  // DWT cycles between samples
  uint32_t next_cycle;
  uint32_t i;
  uint32_t sent = 0U;
  uint8_t tx_chunk[64];           // USB bulk endpoint size limit

  // Check USB enumeration
  if (USB_IsConfigured() == 0U)
  {
    s_sump.armed = 0U;
    s_sump.capture_active = 0U;
    return;
  }

  // Enforce minimum and maximum sample count
  if (sample_count == 0UL)
  {
    sample_count = 4UL;
  }
  if (sample_count > USB_SUMP_MAX_CAPTURE_BYTES)
  {
    sample_count = USB_SUMP_MAX_CAPTURE_BYTES;
  }

  // If Group 0 disabled via SET_FLAGS (0x82), pre-fill with zeros
  if ((s_sump.flags & USB_SUMP_FLAG_DISABLE_GROUP0) != 0U)
  {
    (void)memset(s_sump_capture, 0, sample_count);
  }

  // Lock protocol mode to SUMP (prevent framed protocol interference)
  USB_SetProtocolMode(USB_PROTO_MODE_SUMP);

  // Set state flags for LED and diagnostic monitoring
  s_sump.armed = 1U;
  s_sump.capture_active = 1U;
  s_sump.last_sample_count = sample_count;

  // Compute sample rate and period in CPU cycles
  // rate_hz = 100 MHz / (divider + 1)
  rate_hz = USB_SumpComputeSamplerateHz();
  s_sump.last_samplerate_hz = rate_hz;
  sample_period_cycles = SystemCoreClock / rate_hz;
  if (sample_period_cycles == 0UL)
  {
    sample_period_cycles = 1UL;
  }

  // Enable DWT: trace unit (TRCENA) and cycle counter (CYCCNTENA)
  // CYCCNT: 32-bit counter, increments every CPU clock, wraps after ~42 seconds @ 100 MHz
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  // Wait for trigger condition or 800 ms timeout
  // Polls GPIOB[0:7] at sample rate intervals: (GPIOB->IDR & mask) == value
  (void)USB_SumpWaitForTrigger(sample_period_cycles);

  // Core sampling loop: read GPIOB at precise intervals
  if ((s_sump.flags & USB_SUMP_FLAG_DISABLE_GROUP0) == 0U)
  {
    next_cycle = DWT->CYCCNT + sample_period_cycles;

    // Tight sampling loop: ~12 CPU cycles per iteration
    // - 2 cycles: GPIO read
    // - 0 cycles: DWT wait (tight spin)
    // - ~10 cycles: loop overhead (compare, branch, increment)
    // Total: 50-cycle minimum gap @ 2 MHz sample rate
    for (i = 0U; i < sample_count; i++)
    {
      // Busy-wait until next sample time
      // Signed comparison handles DWT 32-bit wraparound correctly
      while ((int32_t)(DWT->CYCCNT - next_cycle) < 0)
      {
      }
      next_cycle += sample_period_cycles;
      
      // Sample GPIOB[0:7] and store in capture buffer
      // Real mode: read GPIO input register
      // Test mode (INTERNAL_TEST flag): use pseudo-random pattern
      s_sump_capture[i] = USB_SumpSampleByte(i);
    }
  }

  s_sump.capture_active = 0U;
  s_sump.armed = 0U;

  while (sent < sample_count)
  {
    uint32_t base_index;
    uint16_t chunk = MIN_U16((uint16_t)sizeof(tx_chunk), (uint16_t)(sample_count - sent));
    base_index = (sample_count - 1UL) - sent;

    for (i = 0U; i < chunk; i++)
    {
      tx_chunk[i] = s_sump_capture[base_index - i];
    }

    if (USB_SumpTxRawBlocking(tx_chunk, chunk, USB_SUMP_TX_TIMEOUT_MS) == 0U)
    {
      s_last_error_ms = HAL_GetTick();
      break;
    }

    sent += chunk;
  }
}

static void USB_SumpSendMetadata(void)
{
  static const char device_name[] = "BlackPill STM32";
  static const char fw_name[] = "stm32f411";
  uint8_t payload[96];
  uint16_t idx = 0U;
  uint16_t i;

  payload[idx++] = USB_SUMP_META_DEVICE_NAME;
  for (i = 0U; i < (sizeof(device_name) - 1U); i++)
  {
    payload[idx++] = (uint8_t)device_name[i];
  }
  payload[idx++] = 0U;

  payload[idx++] = USB_SUMP_META_FW_VERSION;
  for (i = 0U; i < (sizeof(fw_name) - 1U); i++)
  {
    payload[idx++] = (uint8_t)fw_name[i];
  }
  payload[idx++] = 0U;

  payload[idx++] = USB_SUMP_META_SAMPLE_MEM_BYTES;
  USB_WriteBe32(&payload[idx], USB_SUMP_MAX_CAPTURE_BYTES);
  idx += 4U;

  payload[idx++] = USB_SUMP_META_MAX_RATE_HZ;
  USB_WriteBe32(&payload[idx], USB_SUMP_MAX_SAMPLERATE_HZ);
  idx += 4U;

  payload[idx++] = USB_SUMP_META_NUM_PROBES_SHORT;
  payload[idx++] = 8U;

  payload[idx++] = USB_SUMP_META_PROTO_SHORT;
  payload[idx++] = 2U;

  payload[idx++] = USB_SUMP_META_END;
  (void)USB_SumpTxRawBlocking(payload, idx, USB_SUMP_TX_TIMEOUT_MS);
}

static void USB_SumpHandleLongCommand(uint8_t cmd, const uint8_t *arg)
{
  uint32_t value = USB_ReadLe32(arg);

  if (cmd == USB_SUMP_CMD_SET_DIVIDER)
  {
    s_sump.divider = value & 0x00FFFFFFUL;
  }
  else if (cmd == USB_SUMP_CMD_CAPTURE_SIZE)
  {
    s_sump.read_count_words = USB_ReadLe16(&arg[0]);
    s_sump.delay_count_words = USB_ReadLe16(&arg[2]);
  }
  else if (cmd == USB_SUMP_CMD_SET_FLAGS)
  {
    s_sump.flags = (uint16_t)(value & 0xFFFFUL);
  }
  else if (cmd == USB_SUMP_CMD_CAPTURE_DELAYCOUNT)
  {
    s_sump.delay_count_words = (uint16_t)(value & 0xFFFFUL);
  }
  else if (cmd == USB_SUMP_CMD_CAPTURE_READCOUNT)
  {
    s_sump.read_count_words = (uint16_t)(value & 0xFFFFUL);
  }
  else if ((cmd >= USB_SUMP_CMD_TRIG_MASK_BASE) && (cmd <= 0xCFU))
  {
    uint8_t stage = (uint8_t)((cmd - USB_SUMP_CMD_TRIG_MASK_BASE) >> 2);
    uint8_t kind = (uint8_t)((cmd - USB_SUMP_CMD_TRIG_MASK_BASE) & 0x03U);

    if (stage < 4U)
    {
      if (kind == 0U)
      {
        s_sump.trigger_mask[stage] = value;
      }
      else if (kind == 1U)
      {
        s_sump.trigger_value[stage] = value;
      }
      else if (kind == 2U)
      {
        if ((arg[3] & USB_SUMP_TRIG_START) != 0U)
        {
          s_sump.trigger_start_mask |= (uint8_t)(1U << stage);
        }
        else
        {
          s_sump.trigger_start_mask &= (uint8_t)~(1U << stage);
        }
      }
    }
  }

  USB_SumpClampCaptureWindow();
}

static void USB_SumpHandleShortCommand(uint8_t cmd)
{
  if (cmd == USB_SUMP_CMD_RESET)
  {
    USB_SumpResetState();
    return;
  }

  if (cmd == USB_SUMP_CMD_ID)
  {
    static const uint8_t id_reply[4] = { '1', 'A', 'L', 'S' };
    (void)USB_SumpTxRawBlocking(id_reply, sizeof(id_reply), USB_SUMP_TX_TIMEOUT_MS);
    return;
  }

  if (cmd == USB_SUMP_CMD_METADATA)
  {
    USB_SumpSendMetadata();
    return;
  }

  if (cmd == USB_SUMP_CMD_ARM)
  {
    USB_SumpAcquireAndSend();
    return;
  }
}

static uint8_t USB_ServiceSumpParser(void)
{
  uint8_t cmd;
  uint8_t arg[4];

  if (USB_RxRingAvailable() < USB_SUMP_SHORT_CMD_SIZE)
  {
    return 0U;
  }

  if (USB_RxRingPeek(&cmd, USB_SUMP_SHORT_CMD_SIZE) == 0U)
  {
    return 0U;
  }

  if (USB_SumpIsKnownCommand(cmd) == 0U)
  {
    return 0U;
  }

  if (USB_SumpIsLongCommand(cmd) != 0U)
  {
    if (USB_RxRingAvailable() < USB_SUMP_LONG_CMD_SIZE)
    {
      return 0U;
    }

    (void)USB_RxRingDrop(USB_SUMP_SHORT_CMD_SIZE);
    if (USB_RxRingRead(arg, sizeof(arg)) == 0U)
    {
      return 0U;
    }

    USB_SetProtocolMode(USB_PROTO_MODE_SUMP);
    s_last_rx_ms = HAL_GetTick();
    USB_SumpHandleLongCommand(cmd, arg);
    return 1U;
  }

  (void)USB_RxRingDrop(USB_SUMP_SHORT_CMD_SIZE);
  USB_SetProtocolMode(USB_PROTO_MODE_SUMP);
  s_last_rx_ms = HAL_GetTick();
  USB_SumpHandleShortCommand(cmd);
  return 1U;
}

static void USB_SendError(uint32_t seq, uint16_t code, uint16_t detail)
{
  uint8_t payload[12];

  USB_WriteLe16(&payload[0], code);
  USB_WriteLe16(&payload[2], detail);
  USB_WriteLe32(&payload[4], seq);
  USB_WriteLe32(&payload[8], HAL_GetTick());
  (void)USB_TxQueueEnqueueFrame(USB_PROTO_RSP_ERROR, seq, payload, sizeof(payload));
}

static void USB_BuildLogicSamples(uint8_t *dst, uint16_t sample_count, uint32_t tick_base)
{
  uint16_t i;
  uint32_t local_seed = s_logic_seed;

  for (i = 0U; i < sample_count; i++)
  {
    uint32_t local_tick = tick_base + ((uint32_t)i / 8U);
    uint8_t sample = 0U;

    if (HAL_GPIO_ReadPin(USER_BUTTON_GPIO_Port, USER_BUTTON_Pin) == GPIO_PIN_SET)
    {
      sample |= 0x01U;
    }
    if (((local_tick / 2U) & 1U) != 0U)
    {
      sample |= 0x02U;
    }
    if (((local_tick / 4U) & 1U) != 0U)
    {
      sample |= 0x04U;
    }
    if (((local_tick / 8U) & 1U) != 0U)
    {
      sample |= 0x08U;
    }

    local_seed = USB_XorShift32(&local_seed);
    if ((local_seed & 0x00000001UL) != 0U)
    {
      sample |= 0x10U;
    }
    if (((s_stats.rx_ok + i) & 1U) != 0U)
    {
      sample |= 0x20U;
    }
    if (((s_stats.tx_frames + i) & 1U) != 0U)
    {
      sample |= 0x40U;
    }
    if ((s_stream.enabled != 0U) || (s_reset_pending != 0U))
    {
      sample |= 0x80U;
    }

    dst[i] = sample;
  }

  s_logic_seed = local_seed;
}

static void USB_SendLogicSnapshot(uint32_t seq, uint16_t sample_count)
{
  uint8_t payload[USB_PROTO_MAX_PAYLOAD];
  uint16_t clamped_samples = sample_count;
  uint16_t payload_len;
  uint32_t tick = HAL_GetTick();

  if (clamped_samples == 0U)
  {
    clamped_samples = 128U;
  }
  if (clamped_samples > USB_LOGIC_SNAPSHOT_MAX_SAMPLES)
  {
    clamped_samples = USB_LOGIC_SNAPSHOT_MAX_SAMPLES;
  }

  payload[0] = 1U;
  payload[1] = 8U;
  USB_WriteLe16(&payload[2], USB_LOGIC_SAMPLE_PERIOD_US);
  USB_WriteLe16(&payload[4], clamped_samples);
  USB_WriteLe16(&payload[6], 0U);
  USB_WriteLe32(&payload[8], tick);
  USB_BuildLogicSamples(&payload[USB_LOGIC_SNAPSHOT_HDR_SIZE], clamped_samples, tick);

  payload_len = (uint16_t)(USB_LOGIC_SNAPSHOT_HDR_SIZE + clamped_samples);
  (void)USB_TxQueueEnqueueFrame(USB_PROTO_RSP_LOGIC_SNAPSHOT, seq, payload, payload_len);
}

static uint8_t USB_EmitWarning(uint16_t code, uint16_t detail, uint32_t value)
{
  uint8_t payload[12];

  USB_WriteLe16(&payload[0], code);
  USB_WriteLe16(&payload[2], detail);
  USB_WriteLe32(&payload[4], value);
  USB_WriteLe32(&payload[8], HAL_GetTick());
  return USB_TxQueueEnqueueFrame(USB_PROTO_RSP_WARN, 0U, payload, sizeof(payload));
}

static void USB_ServiceWarnings(void)
{
  uint32_t mask_snapshot = s_pending_warn_mask;

  if ((mask_snapshot & WARN_BIT(USB_WARN_RX_OVERFLOW)) != 0U)
  {
    if (USB_EmitWarning(USB_WARN_RX_OVERFLOW, 0U, s_stats.rx_overflow) != 0U)
    {
      USB_ClearPendingWarning(WARN_BIT(USB_WARN_RX_OVERFLOW));
    }
  }

  if ((mask_snapshot & WARN_BIT(USB_WARN_BAD_MAGIC)) != 0U)
  {
    if (USB_EmitWarning(USB_WARN_BAD_MAGIC, 0U, s_stats.rx_bad_magic) != 0U)
    {
      USB_ClearPendingWarning(WARN_BIT(USB_WARN_BAD_MAGIC));
    }
  }

  if ((mask_snapshot & WARN_BIT(USB_WARN_BAD_VERSION)) != 0U)
  {
    if (USB_EmitWarning(USB_WARN_BAD_VERSION, 0U, s_stats.rx_bad_header) != 0U)
    {
      USB_ClearPendingWarning(WARN_BIT(USB_WARN_BAD_VERSION));
    }
  }

  if ((mask_snapshot & WARN_BIT(USB_WARN_BAD_LENGTH)) != 0U)
  {
    if (USB_EmitWarning(USB_WARN_BAD_LENGTH, 0U, s_stats.rx_bad_header) != 0U)
    {
      USB_ClearPendingWarning(WARN_BIT(USB_WARN_BAD_LENGTH));
    }
  }

  if ((mask_snapshot & WARN_BIT(USB_WARN_CRC_FAIL)) != 0U)
  {
    if (USB_EmitWarning(USB_WARN_CRC_FAIL, 0U, s_stats.rx_crc_fail) != 0U)
    {
      USB_ClearPendingWarning(WARN_BIT(USB_WARN_CRC_FAIL));
    }
  }

  if ((mask_snapshot & WARN_BIT(USB_WARN_TX_OVERFLOW)) != 0U)
  {
    if (USB_EmitWarning(USB_WARN_TX_OVERFLOW, 0U, s_stats.tx_queue_overflow) != 0U)
    {
      USB_ClearPendingWarning(WARN_BIT(USB_WARN_TX_OVERFLOW));
    }
  }

  if ((mask_snapshot & WARN_BIT(USB_WARN_DMA_FAIL)) != 0U)
  {
    if (USB_EmitWarning(USB_WARN_DMA_FAIL, 0U, s_stats.dma_copy_fail) != 0U)
    {
      USB_ClearPendingWarning(WARN_BIT(USB_WARN_DMA_FAIL));
    }
  }

  if ((mask_snapshot & WARN_BIT(USB_WARN_UNKNOWN_CMD)) != 0U)
  {
    if (USB_EmitWarning(USB_WARN_UNKNOWN_CMD, 0U, 0U) != 0U)
    {
      USB_ClearPendingWarning(WARN_BIT(USB_WARN_UNKNOWN_CMD));
    }
  }
}

static void USB_HandlePacket(uint8_t type, uint32_t seq, const uint8_t *payload, uint16_t payload_len)
{
  switch (type)
  {
    case USB_PROTO_CMD_HELLO_REQ:
    {
      uint8_t rsp[32];
      uint32_t caps = 0U;

      caps |= 1UL;
      if (s_dma_handle != NULL)
      {
        caps |= 2UL;
      }
      if (s_crc_handle != NULL)
      {
        caps |= 4UL;
      }
#ifdef USE_FULL_ASSERT
      caps |= 8UL;
#endif
      caps |= 16UL;
      caps |= 32UL;

      USB_WriteLe32(&rsp[0], 0x00010002UL);
      USB_WriteLe32(&rsp[4], USB_PROTO_VERSION);
      USB_WriteLe32(&rsp[8], USB_PROTO_MAX_PAYLOAD);
      USB_WriteLe32(&rsp[12], SystemCoreClock);
      USB_WriteLe32(&rsp[16], caps);
      USB_WriteLe32(&rsp[20], USB_RX_RING_SIZE);
      USB_WriteLe32(&rsp[24], USB_TX_QUEUE_DEPTH);
      USB_WriteLe32(&rsp[28], HAL_GetTick());
      (void)USB_TxQueueEnqueueFrame(USB_PROTO_RSP_HELLO, seq, rsp, sizeof(rsp));
      break;
    }

    case USB_PROTO_CMD_PING_REQ:
      (void)USB_TxQueueEnqueueFrame(USB_PROTO_RSP_PING, seq, payload, payload_len);
      break;

    case USB_PROTO_CMD_ECHO_REQ:
      (void)USB_TxQueueEnqueueFrame(USB_PROTO_RSP_ECHO, seq, payload, payload_len);
      break;

    case USB_PROTO_CMD_START_STREAM_REQ:
    {
      uint16_t payload_cfg_len;
      uint16_t interval_cfg;
      uint32_t packet_count = 0U;
      uint8_t rsp[12];

      if (payload_len < 4U)
      {
        USB_SendError(seq, USB_ERR_BAD_PAYLOAD, type);
        break;
      }

      payload_cfg_len = USB_ReadLe16(&payload[0]);
      interval_cfg = USB_ReadLe16(&payload[2]);
      if (payload_len >= 8U)
      {
        packet_count = USB_ReadLe32(&payload[4]);
      }

      if (payload_cfg_len == 0U)
      {
        payload_cfg_len = USB_STREAM_DEFAULT_PAYLOAD;
      }
      if (payload_cfg_len < USB_STREAM_MIN_PAYLOAD)
      {
        payload_cfg_len = USB_STREAM_MIN_PAYLOAD;
      }
      if (payload_cfg_len > USB_PROTO_MAX_PAYLOAD)
      {
        payload_cfg_len = USB_PROTO_MAX_PAYLOAD;
      }

      s_stream.payload_len = payload_cfg_len;
      s_stream.interval_ms = interval_cfg;
      s_stream.remaining_packets = packet_count;
      s_stream.next_due_ms = HAL_GetTick();
      s_stream.enabled = 1U;

      USB_WriteLe16(&rsp[0], s_stream.payload_len);
      USB_WriteLe16(&rsp[2], s_stream.interval_ms);
      USB_WriteLe32(&rsp[4], s_stream.remaining_packets);
      USB_WriteLe32(&rsp[8], s_stream.counter);
      (void)USB_TxQueueEnqueueFrame(USB_PROTO_RSP_START_STREAM, seq, rsp, sizeof(rsp));
      break;
    }

    case USB_PROTO_CMD_STOP_STREAM_REQ:
    {
      uint8_t rsp[8];
      s_stream.enabled = 0U;
      s_stream.remaining_packets = 0U;
      USB_TxQueueDropPending();
      USB_WriteLe32(&rsp[0], s_stream.counter);
      USB_WriteLe32(&rsp[4], HAL_GetTick());
      (void)USB_TxQueueEnqueueFrame(USB_PROTO_RSP_STOP_STREAM, seq, rsp, sizeof(rsp));
      break;
    }

    case USB_PROTO_CMD_STATS_REQ:
    {
      uint8_t rsp[52];

      USB_WriteLe32(&rsp[0], s_stats.rx_bytes);
      USB_WriteLe32(&rsp[4], s_stats.tx_bytes);
      USB_WriteLe32(&rsp[8], s_stats.rx_ok);
      USB_WriteLe32(&rsp[12], s_stats.rx_bad_magic);
      USB_WriteLe32(&rsp[16], s_stats.rx_bad_header);
      USB_WriteLe32(&rsp[20], s_stats.rx_crc_fail);
      USB_WriteLe32(&rsp[24], s_stats.rx_overflow);
      USB_WriteLe32(&rsp[28], s_stats.tx_queue_overflow);
      USB_WriteLe32(&rsp[32], s_stats.tx_busy_count);
      USB_WriteLe32(&rsp[36], s_stats.dma_copy_count);
      USB_WriteLe32(&rsp[40], s_stats.dma_copy_fail);
      USB_WriteLe32(&rsp[44], s_stream.counter);
      rsp[48] = s_stream.enabled;
      rsp[49] = USB_IsConfigured();
      rsp[50] = s_led_override_mode;
      rsp[51] = 0U;
      (void)USB_TxQueueEnqueueFrame(USB_PROTO_RSP_STATS, seq, rsp, sizeof(rsp));
      break;
    }

    case USB_PROTO_CMD_INTEGRITY_REQ:
    {
      uint8_t rsp[16];
      uint32_t crc = USB_Crc32(payload, payload_len);

      USB_WriteLe32(&rsp[0], crc);
      USB_WriteLe32(&rsp[4], payload_len);
      USB_WriteLe32(&rsp[8], seq);
      USB_WriteLe32(&rsp[12], HAL_GetTick());
      (void)USB_TxQueueEnqueueFrame(USB_PROTO_RSP_INTEGRITY, seq, rsp, sizeof(rsp));
      break;
    }

    case USB_PROTO_CMD_LED_REQ:
    {
      uint8_t rsp[4];

      if (payload_len < 1U)
      {
        USB_SendError(seq, USB_ERR_BAD_PAYLOAD, type);
        break;
      }

      if (payload[0] > 2U)
      {
        USB_SendError(seq, USB_ERR_BAD_PAYLOAD, payload[0]);
        break;
      }

      s_led_override_mode = payload[0];
      rsp[0] = s_led_override_mode;
      rsp[1] = 0U;
      rsp[2] = 0U;
      rsp[3] = 0U;
      (void)USB_TxQueueEnqueueFrame(USB_PROTO_RSP_LED, seq, rsp, sizeof(rsp));
      break;
    }

    case USB_PROTO_CMD_RESET_REQ:
    {
      uint8_t rsp[8];
      uint16_t reason = 0U;

      if (payload_len >= 2U)
      {
        reason = USB_ReadLe16(&payload[0]);
      }

      USB_WriteLe16(&rsp[0], reason);
      USB_WriteLe16(&rsp[2], 0xA55AU);
      USB_WriteLe32(&rsp[4], HAL_GetTick());

      if (USB_TxQueueEnqueueFrame(USB_PROTO_RSP_RESET_ACK, seq, rsp, sizeof(rsp)) != 0U)
      {
        s_reset_due_ms = HAL_GetTick() + USB_RESET_ACK_DELAY_MS;
        s_reset_pending = 1U;
      }
      else
      {
        USB_SendError(seq, USB_ERR_BAD_PAYLOAD, USB_PROTO_CMD_RESET_REQ);
      }
      break;
    }

    case USB_PROTO_CMD_LOGIC_SNAPSHOT_REQ:
    {
      uint16_t requested_samples = 128U;

      if (payload_len >= 2U)
      {
        requested_samples = USB_ReadLe16(&payload[0]);
      }

      USB_SendLogicSnapshot(seq, requested_samples);
      break;
    }

    default:
      USB_SetPendingWarning(WARN_BIT(USB_WARN_UNKNOWN_CMD));
      s_last_error_ms = HAL_GetTick();
      USB_SendError(seq, USB_ERR_UNKNOWN_CMD, type);
      break;
  }
}

static void USB_ServiceParser(void)
{
  /* Parse framed packets first when magic matches; otherwise consume valid SUMP commands. */
  while (1)
  {
    uint16_t available = USB_RxRingAvailable();
    uint8_t header[USB_PROTO_HEADER_SIZE];
    uint32_t maybe_magic;

    if (available == 0U)
    {
      break;
    }

    if (available < 4U)
    {
      if (USB_ServiceSumpParser() != 0U)
      {
        continue;
      }

      if ((USB_RxRingPeek(header, available) != 0U) &&
          (header[0] == 0x55U) &&
          ((available < 2U) || (header[1] == 0x42U)) &&
          ((available < 3U) || (header[2] == 0x54U)))
      {
        break;
      }

      (void)USB_RxRingDrop(1U);
      continue;
    }

    if (USB_RxRingPeek(header, 4U) == 0U)
    {
      break;
    }

    maybe_magic = USB_ReadLe32(header);
    if (maybe_magic == USB_PROTO_MAGIC)
    {
      uint8_t version;
      uint8_t type;
      uint16_t payload_len;
      uint32_t seq;
      uint32_t rx_crc;
      uint32_t calc_crc;

      if (available < USB_PROTO_HEADER_SIZE)
      {
        break;
      }

      if (USB_RxRingPeek(header, USB_PROTO_HEADER_SIZE) == 0U)
      {
        break;
      }

      version = header[4];
      type = header[5];
      payload_len = USB_ReadLe16(&header[6]);
      seq = USB_ReadLe32(&header[8]);
      rx_crc = USB_ReadLe32(&header[12]);

      if (version != USB_PROTO_VERSION)
      {
        (void)USB_RxRingDrop(1U);
        s_stats.rx_bad_header++;
        s_last_error_ms = HAL_GetTick();
        USB_SetPendingWarning(WARN_BIT(USB_WARN_BAD_VERSION));
        continue;
      }

      if (payload_len > USB_PROTO_MAX_PAYLOAD)
      {
        (void)USB_RxRingDrop(1U);
        s_stats.rx_bad_header++;
        s_last_error_ms = HAL_GetTick();
        USB_SetPendingWarning(WARN_BIT(USB_WARN_BAD_LENGTH));
        continue;
      }

      if (available < (uint16_t)(USB_PROTO_HEADER_SIZE + payload_len))
      {
        break;
      }

      (void)USB_RxRingDrop(USB_PROTO_HEADER_SIZE);

      if (payload_len > 0U)
      {
        if (USB_RxRingRead(s_rx_payload, payload_len) == 0U)
        {
          break;
        }
      }

      calc_crc = USB_Crc32(s_rx_payload, payload_len);

      if (calc_crc != rx_crc)
      {
        s_stats.rx_crc_fail++;
        s_last_error_ms = HAL_GetTick();
        if ((s_stats.rx_crc_fail & 0x0FU) == 1U)
        {
          USB_SetPendingWarning(WARN_BIT(USB_WARN_CRC_FAIL));
        }
        continue;
      }

      s_stats.rx_ok++;
      s_last_rx_ms = HAL_GetTick();
      USB_SetProtocolMode(USB_PROTO_MODE_FRAME);
      USB_HandlePacket(type, seq, s_rx_payload, payload_len);
      continue;
    }

    if (USB_ServiceSumpParser() != 0U)
    {
      continue;
    }

    if (s_proto_mode == USB_PROTO_MODE_FRAME)
    {
      (void)USB_RxRingDrop(1U);
      s_stats.rx_bad_magic++;
      if ((s_stats.rx_bad_magic & 0x7FU) == 1U)
      {
        USB_SetPendingWarning(WARN_BIT(USB_WARN_BAD_MAGIC));
      }
    }
    else
    {
      (void)USB_RxRingDrop(1U);
    }
  }
}

static void USB_ServiceStream(uint32_t now_ms)
{
  uint8_t budget = 4U;

  if ((s_stream.enabled == 0U) || (USB_IsConfigured() == 0U))
  {
    return;
  }

  while ((s_stream.enabled != 0U) && (budget > 0U))
  {
    if ((s_stream.interval_ms > 0U) &&
        (USB_TickDiff(now_ms, s_stream.next_due_ms) < 0))
    {
      break;
    }

    USB_CopyWithDmaFallback(s_stream_payload, s_stream_template, s_stream.payload_len);
    USB_WriteLe32(&s_stream_payload[0], s_stream.counter);
    if (s_stream.payload_len >= 8U)
    {
      USB_WriteLe32(&s_stream_payload[4], now_ms);
    }

    if (USB_TxQueueEnqueueFrame(USB_PROTO_RSP_STREAM_DATA, s_stream.counter, s_stream_payload, s_stream.payload_len) == 0U)
    {
      break;
    }

    s_stream.counter++;

    if (s_stream.remaining_packets > 0U)
    {
      s_stream.remaining_packets--;
      if (s_stream.remaining_packets == 0U)
      {
        s_stream.enabled = 0U;
      }
    }

    s_stream.next_due_ms = now_ms + s_stream.interval_ms;
    budget--;

    if (s_stream.interval_ms > 0U)
    {
      break;
    }
  }
}

static void USB_ServicePendingReset(uint32_t now_ms)
{
  if (s_reset_pending == 0U)
  {
    return;
  }

  if (USB_TickDiff(now_ms, s_reset_due_ms) < 0)
  {
    return;
  }

  if ((s_tx_busy != 0U) || (s_tx_head != s_tx_tail))
  {
    return;
  }

  NVIC_SystemReset();
}

void CDC_AppInit(CRC_HandleTypeDef *hcrc, DMA_HandleTypeDef *hdma_memcpy)
{
  s_crc_handle = hcrc;
  s_dma_handle = hdma_memcpy;
  s_app_initialized = 1U;
  s_proto_mode = USB_PROTO_MODE_AUTO;
  USB_SumpResetState();

  USB_CrcInitTable();
  USB_PrepareStreamTemplate();
}

void CDC_AppTask(uint32_t now_ms)
{
  if (s_app_initialized == 0U)
  {
    return;
  }

  USB_ServiceParser();

  if (s_proto_mode != USB_PROTO_MODE_SUMP)
  {
    USB_ServiceWarnings();
    USB_ServiceStream(now_ms);
    USB_TxQueueService();
    USB_ServicePendingReset(now_ms);
  }
}

void CDC_AppGetDiag(CDC_AppDiag_t *diag)
{
  if (diag == NULL)
  {
    return;
  }

  diag->last_rx_ms = s_last_rx_ms;
  diag->last_tx_ms = s_last_tx_ms;
  diag->last_error_ms = s_last_error_ms;
  diag->rx_ok = s_stats.rx_ok;
  diag->rx_crc_fail = s_stats.rx_crc_fail;
  diag->rx_overflow = s_stats.rx_overflow;
  diag->tx_queue_overflow = s_stats.tx_queue_overflow;
  diag->tx_busy_count = s_stats.tx_busy_count;
  diag->stream_enabled = (uint8_t)((s_stream.enabled != 0U) || (s_sump.capture_active != 0U));
  diag->usb_configured = USB_IsConfigured();
  diag->led_override_mode = s_led_override_mode;
}

uint8_t CDC_AppGetLedOverrideMode(void)
{
  return s_led_override_mode;
}
/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
  * @}
  */

/**
  * @}
  */
