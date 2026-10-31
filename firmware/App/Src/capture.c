/**
 * @file    capture.c
 * @brief   TIM1 + DMA2 sampling engine. See capture.h.
 */
#include "capture.h"

#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"

#define DMA_STREAM          DMA2_Stream5
#define DMA_CHANNEL         6U                         /* TIM1_UP, RM0383 table 28 */
#define DMA_ARR_STREAM      DMA2_Stream6
#define DMA_ARR_CHANNEL     0U                         /* TIM1_CH1 */
#define DMA_ARR_FLAGS_CLEAR (DMA_HIFCR_CFEIF6 | DMA_HIFCR_CDMEIF6 | DMA_HIFCR_CTEIF6 |                              DMA_HIFCR_CHTIF6 | DMA_HIFCR_CTCIF6)
#define CAPTURE_TABLE_MAX   32U
#define DMA_FLAGS_CLEAR     (DMA_HIFCR_CFEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CTEIF5 | \
                             DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTCIF5)

_Static_assert((CAPTURE_RING_SIZE % 4U) == 0U, "ring length must be a multiple of 4");
_Static_assert(CAPTURE_RING_SIZE <= 65535U, "DMA NDTR is 16 bit");

static volatile uint8_t s_ring[CAPTURE_RING_SIZE] __attribute__((aligned(4)));
static uint16_t s_arr_table[CAPTURE_TABLE_MAX];

void capture_init(void)
{
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_TIM1_CLK_ENABLE();
}

uint32_t capture_timer_hz(void)
{
  uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();

  /* Timer clock is doubled when the APB prescaler is not 1. */
  return ((RCC->CFGR & RCC_CFGR_PPRE2) >= RCC_CFGR_PPRE2_2) ? (2U * pclk2) : pclk2;
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

uint32_t capture_start(uint32_t sump_divider)
{
  uint64_t num;
  uint64_t den;
  uint64_t g;
  uint32_t psc = 0U;
  uint32_t first;
  uint32_t table_len = 0U;

  capture_stop();

  if (sump_divider < (CAPTURE_MIN_SUMP_PERIOD - 1U))
  {
    sump_divider = CAPTURE_MIN_SUMP_PERIOD - 1U;
  }

  /* Period in timer cycles = num / den, reduced. For a 96 MHz timer den divides 25. */
  num = (uint64_t)(sump_divider + 1U) * capture_timer_hz();
  den = CAPTURE_SUMP_CLOCK_HZ;
  g = gcd64(num, den);
  num /= g;
  den /= g;

  if ((num / den) >= 65536U)
  {
    /* Slow rates: prescale; the rounding error is below 1e-5 of the period. */
    uint64_t cycles = (num + den / 2U) / den;

    psc = (uint32_t)((cycles - 1U) >> 16);
    first = (uint32_t)(((cycles + psc / 2U) / (psc + 1U)) - 1U);
  }
  else if (den == 1U)
  {
    first = (uint32_t)num - 1U;
  }
  else if (den <= CAPTURE_TABLE_MAX)
  {
    /* Period k lasts floor((k+1)*P) - floor(k*P) cycles: exact on average, +-1 cycle (10.4 ns) at most.
     * s_arr_table[j] holds the reload for period j+1; period 0 is loaded directly. */
    for (uint32_t k = 0U; k < den; k++)
    {
      uint64_t c = ((k + 2U) * num) / den - ((k + 1U) * num) / den;

      s_arr_table[k] = (uint16_t)(c - 1U);
    }
    first = (uint32_t)(num / den) - 1U;   /* floor(P) - 1 == c_0 - 1 */
    table_len = (uint32_t)den;
  }
  else
  {
    first = (uint32_t)((num + den / 2U) / den) - 1U;
  }

  /* DMA first: the streams idle until TIM1 raises requests. */
  DMA2->HIFCR = DMA_FLAGS_CLEAR | DMA_ARR_FLAGS_CLEAR;
  DMA_STREAM->PAR  = (uint32_t)&GPIOB->IDR;        /* byte access = PB0..PB7 (little endian) */
  DMA_STREAM->M0AR = (uint32_t)s_ring;
  DMA_STREAM->NDTR = CAPTURE_RING_SIZE;
  DMA_STREAM->FCR  = 0U;                           /* direct mode */
  DMA_STREAM->CR   = (DMA_CHANNEL << DMA_SxCR_CHSEL_Pos) |
                     DMA_SxCR_PL |                 /* very high priority */
                     DMA_SxCR_MINC | DMA_SxCR_CIRC;   /* P2M, 8-bit, no peripheral increment */
  DMA_STREAM->CR  |= DMA_SxCR_EN;

  if (table_len != 0U)
  {
    DMA_ARR_STREAM->PAR  = (uint32_t)&TIM1->ARR;
    DMA_ARR_STREAM->M0AR = (uint32_t)s_arr_table;
    DMA_ARR_STREAM->NDTR = table_len;
    DMA_ARR_STREAM->FCR  = 0U;
    DMA_ARR_STREAM->CR   = (DMA_ARR_CHANNEL << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_PL_1 |   /* high */
                           DMA_SxCR_MSIZE_0 | DMA_SxCR_PSIZE_0 |                     /* 16-bit */
                           DMA_SxCR_MINC | DMA_SxCR_CIRC | DMA_SxCR_DIR_0;           /* M2P */
    DMA_ARR_STREAM->CR  |= DMA_SxCR_EN;
  }

  TIM1->CR1  = TIM_CR1_ARPE;                       /* ARR is preloaded: DMA writes apply next period */
  TIM1->DIER = 0U;
  TIM1->PSC  = psc;
  TIM1->ARR  = first;
  TIM1->CCR1 = 1U;                                 /* CC1 event early in each period fires the ARR DMA */
  TIM1->CNT  = 0U;
  TIM1->EGR  = TIM_EGR_UG;                         /* load PSC/ARR; requests are masked (DIER=0) */
  TIM1->SR   = 0U;
  TIM1->DIER = TIM_DIER_UDE | ((table_len != 0U) ? TIM_DIER_CC1DE : 0U);
  TIM1->CR1  = TIM_CR1_CEN | TIM_CR1_URS | TIM_CR1_ARPE;

  return sump_divider;
}

void capture_stop(void)
{
  TIM1->CR1  = 0U;
  TIM1->DIER = 0U;
  DMA_STREAM->CR &= ~DMA_SxCR_EN;
  DMA_ARR_STREAM->CR &= ~DMA_SxCR_EN;
  while (((DMA_STREAM->CR | DMA_ARR_STREAM->CR) & DMA_SxCR_EN) != 0U)
  {
  }
  DMA2->HIFCR = DMA_FLAGS_CLEAR | DMA_ARR_FLAGS_CLEAR;
}

uint32_t capture_pos(void)
{
  uint32_t pos = CAPTURE_RING_SIZE - DMA_STREAM->NDTR;

  return (pos >= CAPTURE_RING_SIZE) ? 0U : pos;
}

const volatile uint8_t *capture_ring(void)
{
  return s_ring;
}
