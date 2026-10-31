/**
 * @file    testgen.c
 * @brief  Built-in square-wave pattern on PB0..PB7 for self-test.
 *
 * Enabled by the SUMP "internal test" flag. The pins become OUTPUTS, so do not
 * connect a target while this mode is on. Timer outputs are still readable
 * through GPIOB->IDR, so the sampler captures the pattern without any wiring.
 *
 *   PB4 (TIM3_CH1) 1 MHz 50 %      PB5 (TIM3_CH2) 1 MHz 25 %
 *   PB0 (TIM3_CH3) 1 MHz 75 %      PB1 (TIM3_CH4) 1 MHz 12.5 %
 *   PB6 (TIM4_CH1) 500 kHz 50 %    PB7 (TIM4_CH2) 500 kHz 25 %
 *   PB3 (TIM2_CH2) 100 kHz 50 %    PB2            low (not driven by a timer)
 */
#include "capture.h"

#include "stm32f4xx_hal.h"

#define PWM1_PRELOAD(ch)  ((6U << (4U + 8U * (((ch) - 1U) & 1U))) | (1U << (3U + 8U * (((ch) - 1U) & 1U))))

static void pwm_channel(TIM_TypeDef *tim, uint32_t ch, uint32_t ccr)
{
  volatile uint32_t *ccmr = (ch <= 2U) ? &tim->CCMR1 : &tim->CCMR2;

  *ccmr |= PWM1_PRELOAD(ch);
  (&tim->CCR1)[ch - 1U] = ccr;
  tim->CCER |= (1UL << ((ch - 1U) * 4U));
}

static void pwm_timer(TIM_TypeDef *tim, uint32_t arr)
{
  tim->CR1 = 0U;
  tim->PSC = 0U;
  tim->ARR = arr;
  tim->CCMR1 = 0U;
  tim->CCMR2 = 0U;
  tim->CCER = 0U;
}

void testgen_start(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_TIM2_CLK_ENABLE();
  __HAL_RCC_TIM3_CLK_ENABLE();
  __HAL_RCC_TIM4_CLK_ENABLE();

  /* APB1 timer clock is 96 MHz: ARR+1 = 96 -> 1 MHz, 192 -> 500 kHz, 960 -> 100 kHz. */
  pwm_timer(TIM3, 95U);
  pwm_channel(TIM3, 1U, 48U);
  pwm_channel(TIM3, 2U, 24U);
  pwm_channel(TIM3, 3U, 72U);
  pwm_channel(TIM3, 4U, 12U);
  pwm_timer(TIM4, 191U);
  pwm_channel(TIM4, 1U, 96U);
  pwm_channel(TIM4, 2U, 48U);
  pwm_timer(TIM2, 959U);
  pwm_channel(TIM2, 2U, 480U);

  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
  gpio.Alternate = GPIO_AF2_TIM3;
  HAL_GPIO_Init(GPIOB, &gpio);
  gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
  gpio.Alternate = GPIO_AF2_TIM4;
  HAL_GPIO_Init(GPIOB, &gpio);
  gpio.Pin = GPIO_PIN_3;
  gpio.Alternate = GPIO_AF1_TIM2;
  HAL_GPIO_Init(GPIOB, &gpio);

  TIM2->EGR = TIM_EGR_UG;
  TIM3->EGR = TIM_EGR_UG;
  TIM4->EGR = TIM_EGR_UG;
  TIM2->CR1 = TIM_CR1_CEN;
  TIM3->CR1 = TIM_CR1_CEN;
  TIM4->CR1 = TIM_CR1_CEN;
}

void testgen_stop(void)
{
  GPIO_InitTypeDef gpio = {0};

  TIM2->CR1 = 0U;
  TIM3->CR1 = 0U;
  TIM4->CR1 = 0U;
  TIM2->CCER = TIM3->CCER = TIM4->CCER = 0U;

  gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
             GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLDOWN;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio);
}
