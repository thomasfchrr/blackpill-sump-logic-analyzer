/**
 * @file    sump.h
 * @brief   SUMP / OpenBench Logic Sniffer protocol engine (PulseView "ols" driver).
 */
#ifndef SUMP_H
#define SUMP_H

#include <stdint.h>

#define SUMP_FW_VERSION       "2.0.0"
#define SUMP_CHANNELS         8U
/** Longest capture in samples. The ring keeps SUMP_MARGIN samples of slack for polling latency. */
#define SUMP_MARGIN           4092U
#define SUMP_MAX_SAMPLES      61440U
#define SUMP_BASE_CLOCK_HZ    100000000UL   /* clock assumed by every SUMP host for the divider */

typedef enum
{
  SUMP_IDLE = 0,
  SUMP_WAIT_PRETRIGGER,   /* filling the pre-trigger history          */
  SUMP_WAIT_TRIGGER,      /* scanning for the trigger condition        */
  SUMP_POST_TRIGGER,      /* trigger found, collecting the tail        */
  SUMP_SENDING            /* uploading the window, newest sample first */
} sump_state_t;

/** Counters readable from a debugger. */
typedef struct
{
  uint32_t captures;
  uint32_t overruns;        /* window partly overwritten before upload */
  uint32_t missed_scan;     /* trigger scanner fell behind the sampler  */
  uint32_t aborted;
  uint32_t last_divider;    /* timer divider of the last capture        */
  uint32_t last_samples;
} sump_stats_t;

void sump_init(void);
void sump_task(void);
sump_state_t sump_state(void);
const sump_stats_t *sump_stats(void);

#endif /* SUMP_H */
