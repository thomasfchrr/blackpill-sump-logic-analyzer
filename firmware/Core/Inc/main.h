/**
  ******************************************************************************
  * @file    main.h
  * @brief   Board pin definitions and shared declarations.
  ******************************************************************************
  */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

void Error_Handler(void);
void SystemClock_Config(void);

/* BlackPill (STM32F411CEU6) */
#define USER_LED_Pin          GPIO_PIN_13
#define USER_LED_GPIO_Port    GPIOC
#define USER_BUTTON_Pin       GPIO_PIN_0
#define USER_BUTTON_GPIO_Port GPIOA

/* Logic analyzer probes: CH0..CH7 = PB0..PB7 (sampled as one byte of GPIOB->IDR). */
#define PROBE_PORT            GPIOB
#define PROBE_PINS            (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | \
                               GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7)

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
