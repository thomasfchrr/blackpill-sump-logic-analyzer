/**
 * @file    capture.h
 * @brief   Hardware sampling engine: TIM1 update event -> DMA2 -> SRAM ring.
 *
 * TIM1 paces the sampling. Each update event raises a DMA request; DMA2 stream 5
 * (channel 6) copies the low byte of GPIOB->IDR (PB0..PB7) into a circular
 * buffer without any CPU involvement. The CPU only reads the DMA position.
 */
#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdint.h>

/** Ring length in samples. NDTR is 16 bit, and the length must be a multiple of 4. */
#define CAPTURE_RING_SIZE   65532U
/** Clock that every SUMP host assumes when it computes the divider. */
#define CAPTURE_SUMP_CLOCK_HZ  100000000UL
/** Shortest sample period the DMA pair can sustain, in SUMP clock ticks (see capture.c). */
#ifndef CAPTURE_MIN_SUMP_PERIOD
#define CAPTURE_MIN_SUMP_PERIOD  12U
#endif

void     capture_init(void);
/**
 * Start free-running sampling with a period of (sump_divider + 1) / 100 MHz, exactly, on average.
 * The timer runs at 96 MHz, so the reload value is modulated sample by sample (Bresenham style) to
 * remove the 100/96 clock mismatch. Returns the SUMP divider actually used (clamped to the minimum).
 */
uint32_t capture_start(uint32_t sump_divider);
void     capture_stop(void);
/** Index of the next sample the DMA will write, 0 .. CAPTURE_RING_SIZE-1. */
uint32_t capture_pos(void);
const volatile uint8_t *capture_ring(void);
/** Frequency of the TIM1 counter clock in Hz. */
uint32_t capture_timer_hz(void);

/** Drive a known pattern on PB0..PB7 (see testgen.c). Pins are outputs while active! */
void testgen_start(void);
void testgen_stop(void);

#endif /* CAPTURE_H */
