/**
 * @file    led.h
 * @brief   Status LED (PC13, active low on the BlackPill).
 */
#ifndef LED_H
#define LED_H

#include <stdint.h>

typedef enum
{
  LED_NO_USB = 0,   /* slow blink: not enumerated        */
  LED_READY,        /* short flash every 2 s: idle       */
  LED_ARMED,        /* fast blink: waiting for trigger   */
  LED_SENDING,      /* solid on: uploading samples       */
  LED_ERROR         /* very fast blink for one second    */
} led_state_t;

void led_set_state(led_state_t state);
void led_task(uint32_t now_ms);

#endif /* LED_H */
