/**
 * @file    led.c
 * @brief   Status LED patterns.
 */
#include "led.h"

#include "main.h"

static led_state_t s_state = LED_NO_USB;
static uint32_t s_error_until_ms;

static void led_write(int on)
{
  HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void led_set_state(led_state_t state)
{
  if (state == LED_ERROR)
  {
    s_error_until_ms = HAL_GetTick() + 1000U;
  }
  s_state = state;
}

void led_task(uint32_t now_ms)
{
  led_state_t st = s_state;

  if ((st == LED_ERROR) && ((int32_t)(now_ms - s_error_until_ms) >= 0))
  {
    s_state = st = LED_READY;
  }

  switch (st)
  {
    case LED_NO_USB:  led_write(((now_ms / 500U) & 1U) != 0U); break;
    case LED_READY:   led_write((now_ms % 2000U) < 60U);       break;
    case LED_ARMED:   led_write(((now_ms / 100U) & 1U) != 0U); break;
    case LED_SENDING: led_write(1);                            break;
    case LED_ERROR:   led_write(((now_ms / 50U) & 1U) != 0U);  break;
  }
}
