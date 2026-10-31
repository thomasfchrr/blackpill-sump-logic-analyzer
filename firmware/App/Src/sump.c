/**
 * @file    sump.c
 * @brief   SUMP protocol parser and capture state machine.
 *
 * Capture flow (all non-blocking, driven by sump_task()):
 *   ARM -> start the free-running DMA sampler -> wait until the pre-trigger
 *   history is filled -> scan the ring for the (multi-stage) trigger -> wait for
 *   the post-trigger tail -> stop -> upload the window newest-sample-first.
 *
 * Sample indexes are tracked as 64-bit "absolute" counters since the sampler
 * started, so a trigger far in the future never wraps.
 */
#include "sump.h"

#include <stdbool.h>
#include <string.h>

#include "capture.h"
#include "led.h"
#include "main.h"
#include "usb_link.h"

/* --- SUMP command set ------------------------------------------------------ */
#define CMD_RESET            0x00U
#define CMD_ARM              0x01U
#define CMD_ID               0x02U
#define CMD_METADATA         0x04U
#define CMD_SET_DIVIDER      0x80U
#define CMD_CAPTURE_SIZE     0x81U
#define CMD_SET_FLAGS        0x82U
#define CMD_DELAY_COUNT      0x83U
#define CMD_READ_COUNT       0x84U
#define CMD_ADV_TRIG_SEL     0x9EU
#define CMD_ADV_TRIG_WRITE   0x9FU
#define CMD_TRIG_BASE        0xC0U      /* 0xC0..0xCF: stage*4 + {mask, value, config} */
#define TRIG_CFG_START       (1U << 3)  /* in byte 3 of the config word: "trigger on match" */

#define FLAG_DISABLE_GROUP0  (1U << 2)
#define FLAG_INTERNAL_TEST   (1U << 11)

#define META_END             0x00U
#define META_NAME            0x01U
#define META_FW_VERSION      0x02U
#define META_NUM_PROBES      0x20U
#define META_SAMPLE_MEM      0x21U
#define META_MAX_RATE        0x23U
#define META_PROTO_VERSION   0x24U

#define TRIG_STAGES          4U
#define SCAN_GUARD           4U          /* newest samples that may still be in the DMA pipeline */
#define SCAN_BATCH           16384U      /* samples scanned per task call, keeps USB responsive  */
#define SEND_CHUNK           256U

typedef struct
{
  uint32_t divider;                 /* SUMP divider, rate = 100 MHz / (divider + 1) */
  uint32_t read_samples;            /* window size                                   */
  uint32_t delay_samples;           /* samples kept from the trigger onwards         */
  uint16_t flags;
  uint8_t  trig_mask[TRIG_STAGES];
  uint8_t  trig_value[TRIG_STAGES];
  uint8_t  trig_start;              /* bit n: stage n fires the trigger              */
} sump_config_t;

static sump_config_t s_cfg;
static sump_state_t  s_state;
static sump_stats_t  s_stats;
static bool          s_was_connected;

static struct
{
  uint32_t total;          /* samples uploaded                              */
  uint32_t pre;            /* samples that must precede the trigger         */
  uint32_t after;          /* samples from the trigger sample, inclusive    */
  uint64_t wr;             /* samples written by the DMA so far             */
  uint32_t last_pos;
  uint64_t scan;           /* next sample to examine                        */
  uint32_t scan_idx;
  uint8_t  stage;
  uint8_t  last_stage;
  uint64_t end;            /* one past the last sample of the window        */
  uint32_t send_idx;       /* ring index of the next sample to upload       */
  uint32_t sent;
  bool     testgen;
} s_cap;

/* --- helpers --------------------------------------------------------------- */
static uint32_t le32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t le16(const uint8_t *p)
{
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static size_t put_be32(uint8_t *dst, uint32_t v)
{
  dst[0] = (uint8_t)(v >> 24);
  dst[1] = (uint8_t)(v >> 16);
  dst[2] = (uint8_t)(v >> 8);
  dst[3] = (uint8_t)v;
  return 4U;
}

static void config_defaults(void)
{
  memset(&s_cfg, 0, sizeof(s_cfg));
  s_cfg.divider = (SUMP_BASE_CLOCK_HZ / 1000000UL) - 1UL;   /* 1 MS/s */
  s_cfg.read_samples = 4096U;
  s_cfg.delay_samples = 2048U;
}

static void trigger_reset(void)
{
  memset(s_cfg.trig_mask, 0, sizeof(s_cfg.trig_mask));
  memset(s_cfg.trig_value, 0, sizeof(s_cfg.trig_value));
  s_cfg.trig_start = 0U;
}

/* Map the host's 100 MHz-based divider to the closest integer divider of the timer clock. */
static uint32_t timer_divider(uint32_t sump_divider)
{
  uint64_t num = ((uint64_t)sump_divider + 1U) * capture_timer_hz();
  uint64_t div = (num + (SUMP_BASE_CLOCK_HZ / 2U)) / SUMP_BASE_CLOCK_HZ;

  if (div < CAPTURE_MIN_DIV)
  {
    div = CAPTURE_MIN_DIV;
  }
  return (uint32_t)div;
}

/* --- capture --------------------------------------------------------------- */
static void testgen_release(void)
{
  if (s_cap.testgen)
  {
    testgen_stop();
    s_cap.testgen = false;
  }
}

static void capture_abort(void)
{
  capture_stop();
  testgen_release();
}

static void capture_arm(void)
{
  uint32_t total = s_cfg.read_samples;
  uint32_t after = s_cfg.delay_samples;
  uint8_t last = 0U;

  if (!usb_link_connected())
  {
    return;
  }
  if (total > SUMP_MAX_SAMPLES)
  {
    total = SUMP_MAX_SAMPLES;
  }
  if (after > total)
  {
    after = total;
  }

  memset(&s_cap, 0, sizeof(s_cap));
  s_cap.total = total;
  s_cap.after = after;
  s_cap.pre = total - after;

  /* The trigger fires at the first stage flagged START; stage 0 when none is flagged. */
  for (uint8_t i = 0U; i < TRIG_STAGES; i++)
  {
    if ((s_cfg.trig_start & (1U << i)) != 0U)
    {
      last = i;
      break;
    }
  }
  s_cap.last_stage = last;

  if ((s_cfg.flags & FLAG_INTERNAL_TEST) != 0U)
  {
    testgen_start();
    s_cap.testgen = true;
  }

  s_stats.last_divider = capture_start(timer_divider(s_cfg.divider));
  s_stats.last_samples = total;
  s_state = SUMP_WAIT_PRETRIGGER;
  led_set_state(LED_ARMED);
}

static void capture_update_position(void)
{
  uint32_t pos = capture_pos();
  uint32_t delta = (pos >= s_cap.last_pos) ? (pos - s_cap.last_pos)
                                           : (CAPTURE_RING_SIZE - s_cap.last_pos + pos);

  s_cap.wr += delta;
  s_cap.last_pos = pos;
}

/**
 * Offset of the first sample in [idx, idx+count) (ring order) with (s & mask) == value,
 * or count when none. Four samples are tested per 32-bit load.
 */
static uint32_t find_match(uint32_t idx, uint32_t count, uint8_t mask, uint8_t value)
{
  const volatile uint8_t *ring = capture_ring();
  const uint32_t m4 = (uint32_t)mask * 0x01010101UL;
  const uint32_t v4 = (uint32_t)value * 0x01010101UL;   /* value is pre-masked by the caller */
  uint32_t off = 0U;

  while (off < count)
  {
    if (((idx & 3U) == 0U) && ((count - off) >= 4U))
    {
      uint32_t x = (*(const volatile uint32_t *)&ring[idx] & m4) ^ v4;   /* zero byte == match */

      if (((x - 0x01010101UL) & ~x & 0x80808080UL) == 0U)
      {
        off += 4U;
        idx += 4U;
        if (idx >= CAPTURE_RING_SIZE)
        {
          idx = 0U;
        }
        continue;
      }
    }
    if ((ring[idx] & mask) == value)
    {
      return off;
    }
    off++;
    if (++idx >= CAPTURE_RING_SIZE)
    {
      idx = 0U;
    }
  }
  return count;
}

static void scan_trigger(void)
{
  uint64_t limit = (s_cap.wr > SCAN_GUARD) ? (s_cap.wr - SCAN_GUARD) : 0U;
  uint32_t budget = SCAN_BATCH;

  /* Samples older than one ring are gone: skip ahead and record that the scan fell behind. */
  if ((limit - s_cap.scan) > (CAPTURE_RING_SIZE - SUMP_MARGIN))
  {
    s_cap.scan = limit - (CAPTURE_RING_SIZE - SUMP_MARGIN);
    s_cap.scan_idx = (uint32_t)(s_cap.scan % CAPTURE_RING_SIZE);
    s_stats.missed_scan++;
  }

  while ((s_cap.scan < limit) && (budget > 0U))
  {
    uint8_t mask = s_cfg.trig_mask[s_cap.stage];
    uint64_t avail = limit - s_cap.scan;
    uint32_t count = (avail > budget) ? budget : (uint32_t)avail;
    uint32_t off = find_match(s_cap.scan_idx, count, mask, s_cfg.trig_value[s_cap.stage] & mask);
    uint32_t used = (off == count) ? count : (off + 1U);

    budget -= used;
    s_cap.scan += used;
    s_cap.scan_idx += used;
    if (s_cap.scan_idx >= CAPTURE_RING_SIZE)
    {
      s_cap.scan_idx -= CAPTURE_RING_SIZE;
    }
    if (off == count)
    {
      continue;
    }
    if (s_cap.stage == s_cap.last_stage)
    {
      s_cap.end = (s_cap.scan - 1U) + s_cap.after;   /* trigger sample is the first "after" sample */
      s_state = SUMP_POST_TRIGGER;
      return;
    }
    s_cap.stage++;
  }
}

static void capture_finish_acquisition(void)
{
  uint64_t lag;
  uint32_t end_idx;
  bool overrun;

  capture_stop();
  capture_update_position();
  testgen_release();

  /* The window is [end - total, end). It is intact only if the ring was not lapped. */
  overrun = (s_cap.wr - (s_cap.end - s_cap.total)) > CAPTURE_RING_SIZE;
  if (overrun)
  {
    s_stats.overruns++;
  }

  lag = (s_cap.wr - s_cap.end) % CAPTURE_RING_SIZE;
  end_idx = (uint32_t)((s_cap.last_pos + CAPTURE_RING_SIZE - lag) % CAPTURE_RING_SIZE);
  s_cap.send_idx = (end_idx + CAPTURE_RING_SIZE - 1U) % CAPTURE_RING_SIZE;
  s_cap.sent = 0U;
  s_stats.captures++;
  s_state = SUMP_SENDING;
  led_set_state(overrun ? LED_ERROR : LED_SENDING);
}

static void send_window(void)
{
  uint8_t chunk[SEND_CHUNK];

  while ((s_cap.sent < s_cap.total) && (usb_link_tx_free() >= SEND_CHUNK))
  {
    uint32_t n = s_cap.total - s_cap.sent;
    const volatile uint8_t *ring = capture_ring();
    uint32_t idx = s_cap.send_idx;
    uint8_t keep = ((s_cfg.flags & FLAG_DISABLE_GROUP0) != 0U) ? 0x00U : 0xFFU;

    if (n > SEND_CHUNK)
    {
      n = SEND_CHUNK;
    }
    for (uint32_t i = 0U; i < n; i++)
    {
      chunk[i] = (uint8_t)(ring[idx] & keep);
      idx = (idx == 0U) ? (CAPTURE_RING_SIZE - 1U) : (idx - 1U);
    }
    (void)usb_link_tx_write(chunk, n);
    s_cap.send_idx = idx;
    s_cap.sent += n;
  }
  if ((s_cap.sent >= s_cap.total) && (usb_link_tx_pending() == 0U))
  {
    s_state = SUMP_IDLE;
    led_set_state(LED_READY);
  }
}

static void capture_service(void)
{
  switch (s_state)
  {
    case SUMP_WAIT_PRETRIGGER:
      capture_update_position();
      if (s_cap.wr >= ((uint64_t)s_cap.pre + SCAN_GUARD))
      {
        s_cap.scan = s_cap.pre;
        s_cap.scan_idx = s_cap.pre;
        s_state = SUMP_WAIT_TRIGGER;
      }
      break;

    case SUMP_WAIT_TRIGGER:
      capture_update_position();
      if (s_cfg.trig_mask[s_cap.last_stage] == 0U)
      {
        s_cap.end = (uint64_t)s_cap.pre + s_cap.after;    /* no condition: trigger immediately */
        s_state = SUMP_POST_TRIGGER;
      }
      else
      {
        scan_trigger();
      }
      break;

    case SUMP_POST_TRIGGER:
      capture_update_position();
      if (s_cap.wr >= (s_cap.end + SCAN_GUARD))
      {
        capture_finish_acquisition();
      }
      break;

    case SUMP_SENDING:
      send_window();
      break;

    case SUMP_IDLE:
    default:
      break;
  }
}

/* --- protocol -------------------------------------------------------------- */
static void send_metadata(void)
{
  static const char name[] = "BlackPill STM32F411 Logic Analyzer";
  uint8_t out[96];
  size_t n = 0U;

  out[n++] = META_NAME;
  memcpy(&out[n], name, sizeof(name));          /* includes the terminating NUL */
  n += sizeof(name);
  out[n++] = META_FW_VERSION;
  memcpy(&out[n], SUMP_FW_VERSION, sizeof(SUMP_FW_VERSION));
  n += sizeof(SUMP_FW_VERSION);

  out[n++] = META_NUM_PROBES;    n += put_be32(&out[n], SUMP_CHANNELS);
  out[n++] = META_SAMPLE_MEM;    n += put_be32(&out[n], SUMP_MAX_SAMPLES);
  out[n++] = META_MAX_RATE;      n += put_be32(&out[n], capture_timer_hz() / CAPTURE_MIN_DIV);
  out[n++] = META_PROTO_VERSION; n += put_be32(&out[n], 2U);
  out[n++] = META_END;
  (void)usb_link_tx_write(out, n);
}

static void handle_long(uint8_t cmd, const uint8_t *arg)
{
  if (cmd == CMD_SET_DIVIDER)
  {
    s_cfg.divider = le32(arg) & 0x00FFFFFFUL;
  }
  else if (cmd == CMD_CAPTURE_SIZE)
  {
    s_cfg.read_samples = ((uint32_t)le16(&arg[0]) + 1U) * 4U;
    s_cfg.delay_samples = ((uint32_t)le16(&arg[2]) + 1U) * 4U;
  }
  else if (cmd == CMD_READ_COUNT)
  {
    s_cfg.read_samples = ((uint32_t)le16(arg) + 1U) * 4U;
  }
  else if (cmd == CMD_DELAY_COUNT)
  {
    s_cfg.delay_samples = ((uint32_t)le16(arg) + 1U) * 4U;
  }
  else if (cmd == CMD_SET_FLAGS)
  {
    s_cfg.flags = le16(arg);
  }
  else if ((cmd >= CMD_TRIG_BASE) && (cmd <= 0xCFU))
  {
    uint8_t stage = (uint8_t)((cmd - CMD_TRIG_BASE) >> 2);
    uint8_t kind = (uint8_t)((cmd - CMD_TRIG_BASE) & 3U);

    if (stage < TRIG_STAGES)
    {
      if (kind == 0U)
      {
        s_cfg.trig_mask[stage] = arg[0];          /* only the first 8 probes exist */
      }
      else if (kind == 1U)
      {
        s_cfg.trig_value[stage] = arg[0];
      }
      else if (kind == 2U)
      {
        if ((arg[3] & TRIG_CFG_START) != 0U)
        {
          s_cfg.trig_start |= (uint8_t)(1U << stage);
        }
        else
        {
          s_cfg.trig_start &= (uint8_t)~(1U << stage);
        }
      }
    }
  }
  /* CMD_ADV_TRIG_*: accepted and ignored. */
}

static void handle_reset(void)
{
  if (s_state != SUMP_IDLE)
  {
    capture_abort();
    usb_link_tx_discard();
    s_stats.aborted++;
    s_state = SUMP_IDLE;
  }
  trigger_reset();
  led_set_state(usb_link_connected() ? LED_READY : LED_NO_USB);
}

static bool is_long(uint8_t cmd)
{
  return (cmd == CMD_SET_DIVIDER) || (cmd == CMD_CAPTURE_SIZE) || (cmd == CMD_SET_FLAGS) ||
         (cmd == CMD_DELAY_COUNT) || (cmd == CMD_READ_COUNT) ||
         (cmd == CMD_ADV_TRIG_SEL) || (cmd == CMD_ADV_TRIG_WRITE) ||
         ((cmd >= CMD_TRIG_BASE) && (cmd <= 0xCFU));
}

static void parse_rx(void)
{
  uint8_t buf[5];

  while (usb_link_rx_available() > 0U)
  {
    (void)usb_link_rx_peek(buf, 1U);

    /* While a capture runs only RESET is honoured, everything else is dropped. */
    if (s_state != SUMP_IDLE)
    {
      usb_link_rx_drop(1U);
      if (buf[0] == CMD_RESET)
      {
        handle_reset();
      }
      continue;
    }

    if (is_long(buf[0]))
    {
      if (usb_link_rx_peek(buf, 5U) < 5U)
      {
        return;   /* wait for the rest of the command */
      }
      usb_link_rx_drop(5U);
      handle_long(buf[0], &buf[1]);
      continue;
    }

    usb_link_rx_drop(1U);
    switch (buf[0])
    {
      case CMD_RESET:    handle_reset(); break;
      case CMD_ARM:      capture_arm(); break;
      case CMD_ID:       (void)usb_link_tx_write((const uint8_t *)"1ALS", 4U); break;
      case CMD_METADATA: send_metadata(); break;
      default:           break;   /* XON/XOFF/self-test/unknown: ignored */
    }
  }
}

void sump_init(void)
{
  config_defaults();
  memset(&s_stats, 0, sizeof(s_stats));
  s_state = SUMP_IDLE;
  s_was_connected = false;
}

void sump_task(void)
{
  bool connected = usb_link_connected();

  if (connected != s_was_connected)
  {
    s_was_connected = connected;
    led_set_state(connected ? LED_READY : LED_NO_USB);
  }
  if (!connected)
  {
    if (s_state != SUMP_IDLE)
    {
      capture_abort();
      s_state = SUMP_IDLE;
    }
    return;
  }

  parse_rx();
  capture_service();
  usb_link_task();
}

sump_state_t sump_state(void)
{
  return s_state;
}

const sump_stats_t *sump_stats(void)
{
  return &s_stats;
}
