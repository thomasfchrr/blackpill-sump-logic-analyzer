/**
 * @file    capture.c
 * @brief   Sampling engine with an exact SUMP timebase. See capture.h.
 *
 *   TIM5 (master, APB1, 32 bit)  --TRGO on every update-->  TIM1 (slave, reset mode)
 *      ^ ARR reload table                                      | update event = DMA request
 *      | DMA1 stream 2 (TIM5_CH1 request)                      v
 *                                              DMA2 stream 5: GPIOB->IDR byte --> SRAM ring
 *
 * The 96 MHz timer clock cannot divide into 10 ns SUMP ticks, so TIM5 changes its reload value every
 * period (5,5,5,5,4 cycles for a 4.8-cycle period, ...). That reload traffic runs on DMA1 and the
 * APB1 bridge, while DMA2 only has to copy one byte per sample.
 */
#include "capture.h"

#include <stdbool.h>

#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"

#define SAMPLE_DMA          DMA2_Stream5
#define SAMPLE_CHANNEL      6U                         /* TIM1_UP request */
#define SAMPLE_FLAGS        (DMA_HIFCR_CFEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CTEIF5 | \
                             DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTCIF5)

#define RELOAD_DMA          DMA1_Stream2
#define RELOAD_CHANNEL      6U                         /* TIM5_CH1 request */
#define RELOAD_FLAGS        (DMA_LIFCR_CFEIF2 | DMA_LIFCR_CDMEIF2 | DMA_LIFCR_CTEIF2 | \
                             DMA_LIFCR_CHTIF2 | DMA_LIFCR_CTCIF2)
#define RELOAD_TABLE_MAX    32U
#define RELOAD_LATENCY_MIN  5U                         /* plausible window for the calibrated value */
#define RELOAD_LATENCY_MAX  16U
#ifndef RELOAD_LATENCY
#define RELOAD_LATENCY      10U                        /* fallback if the calibration gives nonsense */
#endif
#ifndef RELOAD_LATENCY_BIAS
#define RELOAD_LATENCY_BIAS 1                          /* used = measured + bias; +1 centres the value in the 9..11 window that keeps periods of 10+ ticks exact */
#endif

_Static_assert((CAPTURE_RING_SIZE % 4U) == 0U, "ring length must be a multiple of 4");
_Static_assert(CAPTURE_RING_SIZE <= 65535U, "DMA NDTR is 16 bit");

static volatile uint8_t s_ring[CAPTURE_RING_SIZE] __attribute__((aligned(4)));
static uint32_t s_reload_table[RELOAD_TABLE_MAX];

static uint32_t s_latency = RELOAD_LATENCY;
static volatile uint32_t s_cal_value;
static volatile uint32_t s_measured_latency;   /* raw calibration result, readable from a debugger */

/**
 * One calibration probe. TIM5 free-runs over 64 cycles; at count k its CC1 event makes DMA1 write
 * CAL_TARGET into CCR3 (the same memory-to-APB1 path as a real reload). CCR3 starts out of reach, so
 * CC3IF is set only if the write landed before the counter got to CAL_TARGET.
 */
#define CAL_PERIOD  64U
#define CAL_TARGET  48U

static bool calibration_probe(uint32_t k)
{
  uint32_t guard = 100000U;
  bool hit;

  s_cal_value = CAL_TARGET;
  RELOAD_DMA->CR = 0U;
  DMA1->LIFCR = RELOAD_FLAGS;
  RELOAD_DMA->PAR  = (uint32_t)&TIM5->CCR3;
  RELOAD_DMA->M0AR = (uint32_t)&s_cal_value;
  RELOAD_DMA->NDTR = 1U;
  RELOAD_DMA->FCR  = 0U;
  RELOAD_DMA->CR   = (RELOAD_CHANNEL << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_PL_1 |
                     DMA_SxCR_MSIZE_1 | DMA_SxCR_PSIZE_1 | DMA_SxCR_DIR_0;     /* normal mode, M2P */
  RELOAD_DMA->CR  |= DMA_SxCR_EN;

  TIM5->CR1  = 0U;
  TIM5->CR2  = 0U;
  TIM5->DIER = 0U;
  TIM5->PSC  = 0U;
  TIM5->ARR  = CAL_PERIOD - 1U;
  TIM5->CCR1 = k;
  TIM5->CCR3 = 0xFFFFFFFFU;
  TIM5->CNT  = 0U;
  TIM5->EGR  = TIM_EGR_UG;
  TIM5->SR   = 0U;
  TIM5->DIER = TIM_DIER_CC1DE;
  TIM5->CR1  = TIM_CR1_CEN;
  while (((TIM5->SR & TIM_SR_UIF) == 0U) && (--guard != 0U))
  {
  }
  hit = (TIM5->SR & TIM_SR_CC3IF) != 0U;
  TIM5->CR1  = 0U;
  TIM5->DIER = 0U;
  RELOAD_DMA->CR = 0U;
  while ((RELOAD_DMA->CR & DMA_SxCR_EN) != 0U)
  {
  }
  return hit;
}

/** Measure the delay between a CC1 event and the landing of the DMA1 write, in timer cycles. */
static uint32_t calibrate_reload_latency(void)
{
  uint32_t last_hit_k = 0U;

  for (uint32_t k = 1U; k < CAL_TARGET; k++)
  {
    bool all = true;

    for (uint32_t rep = 0U; rep < 5U; rep++)
    {
      all = all && calibration_probe(k);     /* the slowest of 5 runs decides */
    }
    if (all)
    {
      last_hit_k = k;
    }
    else if (last_hit_k != 0U)
    {
      break;
    }
  }
  return (last_hit_k == 0U) ? 0U : (CAL_TARGET - last_hit_k);
}

void capture_init(void)
{
  uint32_t measured;
  int32_t used;

  __HAL_RCC_DMA1_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_TIM1_CLK_ENABLE();
  __HAL_RCC_TIM5_CLK_ENABLE();

  measured = calibrate_reload_latency();
  used = (int32_t)measured + RELOAD_LATENCY_BIAS;
  s_latency = ((used >= (int32_t)RELOAD_LATENCY_MIN) && (used <= (int32_t)RELOAD_LATENCY_MAX))
                ? (uint32_t)used : RELOAD_LATENCY;
  s_measured_latency = measured;
}

uint32_t capture_reload_latency(void)
{
  return s_latency;
}

uint32_t capture_timer_hz(void)
{
  uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();

  /* Timer clock is doubled when the APB prescaler is not 1. TIM5 and TIM1 must share it. */
  return ((RCC->CFGR & RCC_CFGR_PPRE1) >= RCC_CFGR_PPRE1_2) ? (2U * pclk1) : pclk1;
}

static uint64_t gcd64(uint64_t a, uint64_t b)
{
  while (b != 0U)
  {
    uint64_t t = a % b;
    a = b;
    b = t;
  }
  return a;
}

/* Length in timer cycles of period k when the mean period is num / den cycles. */
static uint32_t reload_cycles(uint64_t num, uint64_t den, uint32_t k)
{
  k %= (uint32_t)den;
  return (uint32_t)(((k + 1U) * num) / den - (k * num) / den);
}

uint32_t capture_start(uint32_t sump_divider)
{
  uint64_t num;
  uint64_t den;
  uint64_t g;
  uint32_t first;
  uint32_t second;
  uint32_t request_at;
  uint32_t table_len = 0U;

  capture_stop();

  if (sump_divider < (CAPTURE_MIN_SUMP_PERIOD - 1U))
  {
    sump_divider = CAPTURE_MIN_SUMP_PERIOD - 1U;
  }

  /* Period in timer cycles = num / den, reduced (den divides 25 for a 96 MHz timer). */
  num = (uint64_t)(sump_divider + 1U) * capture_timer_hz();
  den = CAPTURE_SUMP_CLOCK_HZ;
  g = gcd64(num, den);
  num /= g;
  den /= g;

  /* Period k lasts c(k) = floor((k+1) P) - floor(k P) cycles: exact on average, at most 1 cycle (10.4 ns)
   * of jitter. A reload request raised K cycles into period n reaches TIM5->ARR s_latency cycles (measured at boot)
   * later, i.e. in period n + q with q = (K + latency) / P, and then applies to period n + q + 1.
   * K is chosen so the write lands in the middle of a period, whatever the rate: q is the smallest
   * depth for which K = (q + 1/2) P - latency fits in the period. Entry n holds c(n + q + 1). The
   * first q + 1 periods use c(0) (a fixed offset of a few cycles at the start of the capture, no
   * influence on the rate). */
  first = (uint32_t)(num / den) - 1U;
  if ((den > 1U) && (den <= RELOAD_TABLE_MAX))
  {
    uint32_t p2 = (uint32_t)((2U * num) / den);       /* 2 P, in cycles, rounded down */
    uint32_t pmin = (uint32_t)(num / den);
    uint32_t depth = 0U;

    table_len = (uint32_t)den;
    request_at = 1U;
    for (depth = 0U; depth < 8U; depth++)
    {
      uint32_t land = (2U * depth + 1U) * p2 / 4U;      /* (q + 1/2) P */

      if (land > s_latency)
      {
        request_at = land - s_latency;
        if (request_at < 1U)
        {
          request_at = 1U;
        }
        if (request_at <= (pmin - 1U))
        {
          break;
        }
      }
    }
    for (uint32_t j = 0U; j < table_len; j++)
    {
      s_reload_table[j] = reload_cycles(num, den, j + depth + 1U) - 1U;
    }
    first = reload_cycles(num, den, 0U) - 1U;
  }
  else
  {
    request_at = 1U;
  }
  second = first;

  /* Pacer: TIM5 with a preloaded 32-bit ARR that DMA1 rewrites once per period. */
  RELOAD_DMA->CR = 0U;
  DMA1->LIFCR = RELOAD_FLAGS;
  TIM5->CR1  = TIM_CR1_ARPE;
  TIM5->CR2  = TIM_CR2_MMS_1;                      /* TRGO = update event */
  TIM5->DIER = 0U;
  TIM5->PSC  = 0U;
  TIM5->ARR  = first;
  TIM5->CCR1 = request_at;                         /* CC1 late in each period requests the reload */
  TIM5->CNT  = 0U;
  TIM5->EGR  = TIM_EGR_UG;                         /* loads ARR; TIM1 is still disabled, so it ignores the TRGO */
  TIM5->ARR  = second;                             /* preload: period 1 */
  TIM5->SR   = 0U;
  if (table_len != 0U)
  {
    RELOAD_DMA->PAR  = (uint32_t)&TIM5->ARR;
    RELOAD_DMA->M0AR = (uint32_t)s_reload_table;
    RELOAD_DMA->NDTR = table_len;
    RELOAD_DMA->FCR  = 0U;
    RELOAD_DMA->CR   = (RELOAD_CHANNEL << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_PL_1 |
                       DMA_SxCR_MSIZE_1 | DMA_SxCR_PSIZE_1 |                /* 32-bit */
                       DMA_SxCR_MINC | DMA_SxCR_CIRC | DMA_SxCR_DIR_0;      /* memory to peripheral */
    RELOAD_DMA->CR  |= DMA_SxCR_EN;
    TIM5->DIER = TIM_DIER_CC1DE;
  }
  /* Sampler: each TIM5 trigger resets TIM1, whose update event is the DMA request. */
  SAMPLE_DMA->CR = 0U;
  DMA2->HIFCR = SAMPLE_FLAGS;
  SAMPLE_DMA->PAR  = (uint32_t)&GPIOB->IDR;        /* byte access = PB0..PB7 (little endian) */
  SAMPLE_DMA->M0AR = (uint32_t)s_ring;
  SAMPLE_DMA->NDTR = CAPTURE_RING_SIZE;
  SAMPLE_DMA->FCR  = 0U;                           /* direct mode */
  SAMPLE_DMA->CR   = (SAMPLE_CHANNEL << DMA_SxCR_CHSEL_Pos) |
                     DMA_SxCR_PL |                 /* very high priority */
                     DMA_SxCR_MINC | DMA_SxCR_CIRC;   /* P2M, 8-bit, no peripheral increment */
  SAMPLE_DMA->CR  |= DMA_SxCR_EN;

  TIM1->CR1  = 0U;
  TIM1->DIER = 0U;
  TIM1->PSC  = 0xFFFFU;                            /* never overflows between two triggers */
  TIM1->ARR  = 0xFFFFU;
  TIM1->SMCR = TIM_SMCR_SMS_2 | 0U;                /* reset mode, TS = ITR0 = TIM5_TRGO: each trigger
                                                      resets the counter and raises an update event */
  TIM1->CNT  = 0U;
  TIM1->EGR  = TIM_EGR_UG;
  TIM1->SR   = 0U;
  TIM1->DIER = TIM_DIER_UDE;
  TIM1->CR1  = TIM_CR1_CEN;

  TIM5->CR1 = TIM_CR1_CEN | TIM_CR1_URS | TIM_CR1_ARPE;

  return sump_divider;
}

void capture_stop(void)
{
  TIM5->CR1  = 0U;
  TIM5->DIER = 0U;
  TIM1->CR1  = 0U;
  TIM1->DIER = 0U;
  SAMPLE_DMA->CR &= ~DMA_SxCR_EN;
  RELOAD_DMA->CR &= ~DMA_SxCR_EN;
  while (((SAMPLE_DMA->CR | RELOAD_DMA->CR) & DMA_SxCR_EN) != 0U)
  {
  }
  DMA2->HIFCR = SAMPLE_FLAGS;
  DMA1->LIFCR = RELOAD_FLAGS;
}

uint32_t capture_pos(void)
{
  uint32_t pos = CAPTURE_RING_SIZE - SAMPLE_DMA->NDTR;

  return (pos >= CAPTURE_RING_SIZE) ? 0U : pos;
}

const volatile uint8_t *capture_ring(void)
{
  return s_ring;
}
