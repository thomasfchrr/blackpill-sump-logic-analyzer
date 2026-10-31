/**
  ******************************************************************************
  * @file    main.c
  * @brief   BlackPill STM32F411 SUMP logic analyzer: start-up and main loop.
  *
  * Clock tree: HSE 25 MHz -> PLL (M=25, N=192, P=2, Q=4) -> SYSCLK 96 MHz, USB 48 MHz.
  * This file is maintained by hand; the .ioc only documents clocks and USB.
  ******************************************************************************
  */
#include "main.h"

#include "capture.h"
#include "led.h"
#include "sump.h"
#include "usb_device.h"

static void MX_GPIO_Init(void);

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  capture_init();
  sump_init();
  MX_USB_DEVICE_Init();

  while (1)
  {
    sump_task();
    led_task(HAL_GetTick());
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_ON;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM = 25;                     /* 25 MHz / 25 = 1 MHz VCO input  */
  osc.PLL.PLLN = 192;                    /* VCO = 192 MHz                  */
  osc.PLL.PLLP = RCC_PLLP_DIV2;          /* SYSCLK = 96 MHz                */
  osc.PLL.PLLQ = 4;                      /* USB = 48 MHz                   */
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    Error_Handler();
  }

  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV2;    /* 48 MHz (max 50)                */
  clk.APB2CLKDivider = RCC_HCLK_DIV1;    /* 96 MHz: TIM1 pacing clock      */
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_RCC_EnableCSS();   /* HSE failure -> NMI (see NMI_Handler) */
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_SET);   /* LED off (active low) */
  gpio.Pin = USER_LED_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(USER_LED_GPIO_Port, &gpio);

  gpio.Pin = USER_BUTTON_Pin;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(USER_BUTTON_GPIO_Port, &gpio);

  /* Probes: inputs with a weak pull-down so open channels read a stable 0. */
  gpio.Pin = PROBE_PINS;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLDOWN;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(PROBE_PORT, &gpio);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
    /* Fast blink without SysTick (interrupts are off). */
    HAL_GPIO_TogglePin(USER_LED_GPIO_Port, USER_LED_Pin);
    for (volatile uint32_t i = 0; i < 2000000U; i++)
    {
    }
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
  Error_Handler();
}
#endif

/* Newlib start-up hooks, unused in a bare-metal image. */
void _init(void) {}
void _fini(void) {}
