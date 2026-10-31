/**
 * @file    capture.c
 * @brief   TIM1 + DMA2 sampling engine. See capture.h.
 */
#include "capture.h"

#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"

#define DMA_STREAM          DMA2_Stream5
#define DMA_CHANNEL         6U                         /* TIM1_UP, RM0383 table 28 */
#define DMA_FLAGS_CLEAR     (DMA_HIFCR_CFEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CTEIF5 | \
                             DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTCIF5)

_Static_assert((CAPTURE_RING_SIZE % 4U) == 0U, "ring length must be a multiple of 4");
_Static_assert(CAPTURE_RING_SIZE <= 65535U, "DMA NDTR is 16 bit");

static volatile uint8_t s_ring[CAPTURE_RING_SIZE] __attribute__((aligned(4)));

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

uint32_t capture_start(uint32_t div)
{
  uint32_t psc = 0U;
  uint32_t arr;

  capture_stop();

  if (div < CAPTURE_MIN_DIV)
  {
    div = CAPTURE_MIN_DIV;
  }
  if (div > 65536U)
  {
    psc = (div - 1U) >> 16;                        /* keep ARR within 16 bit */
  }
  arr = ((div + (psc + 1U) / 2U) / (psc + 1U)) - 1U;
  if (arr > 0xFFFFU)
  {
    arr = 0xFFFFU;
  }

  /* DMA first: it idles until TIM1 raises requests. */
  DMA2->HIFCR = DMA_FLAGS_CLEAR;
  DMA_STREAM->PAR  = (uint32_t)&GPIOB->IDR;        /* byte access = PB0..PB7 (little endian) */
  DMA_STREAM->M0AR = (uint32_t)s_ring;
  DMA_STREAM->NDTR = CAPTURE_RING_SIZE;
  DMA_STREAM->FCR  = 0U;                           /* direct mode */
  DMA_STREAM->CR   = (DMA_CHANNEL << DMA_SxCR_CHSEL_Pos) |
                     DMA_SxCR_PL |                 /* very high priority */
                     DMA_SxCR_MINC | DMA_SxCR_CIRC;   /* P2M, 8-bit, no peripheral increment */
  DMA_STREAM->CR  |= DMA_SxCR_EN;

  TIM1->CR1  = 0U;
  TIM1->DIER = 0U;
  TIM1->PSC  = psc;
  TIM1->ARR  = arr;
  TIM1->CNT  = 0U;
  TIM1->EGR  = TIM_EGR_UG;                         /* load PSC/ARR; request is masked (DIER=0) */
  TIM1->SR   = 0U;
  TIM1->DIER = TIM_DIER_UDE;
  TIM1->CR1  = TIM_CR1_CEN | TIM_CR1_URS;

  return (arr + 1U) * (psc + 1U);
}

void capture_stop(void)
{
  TIM1->CR1  = 0U;
  TIM1->DIER = 0U;
  DMA_STREAM->CR &= ~DMA_SxCR_EN;
  while ((DMA_STREAM->CR & DMA_SxCR_EN) != 0U)
  {
  }
  DMA2->HIFCR = DMA_FLAGS_CLEAR;
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
