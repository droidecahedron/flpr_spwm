/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 *  @brief Bit-pattern compiler for the FLPR streamer.
 *
 *  A step is one carrier period, the analogue of one PWM SEQ value. Steps compile to
 *  2-bit frames, 16 per 32-bit word, LSB first, frame bits [1:0] = {leg B, leg A}.
 */

#ifndef SPWM_PATTERN_H_
#define SPWM_PATTERN_H_

#include <stddef.h>
#include <stdint.h>

enum spwm_leg {
	SPWM_LEG_NONE = 0,
	SPWM_LEG_A,
	SPWM_LEG_B,
};

struct spwm_step {
	uint8_t leg;           /* enum spwm_leg */
	uint16_t dead_ticks;   /* minimum low time before and after the pulse */
	uint16_t on_ticks;     /* pulse width, centered in the period */
	uint16_t period_ticks;
};

/**
 * @brief Fill steps from a natural-sampled sine, sampled at mid-period.
 *
 * Sign picks the leg, |sin| * depth scales the pulse between 0 and period - 2 * dead.
 *
 * @param f_ref         reference (sine) frequency, Hz
 * @param carrier_ratio carrier periods per reference period
 * @param depth_pct     modulation depth, 0-100
 * @param dead_ticks    dead time on both sides of every pulse
 * @param tick_hz       tick rate, 128 MHz / (cnttop + 1)
 * @param steps         output, n entries
 * @param n             steps to fill. A multiple of carrier_ratio gives whole cycles.
 *
 * @retval 0 on success
 * @retval -EINVAL if the carrier period doesn't fit the dead time or 16 bits
 */
int spwm_sine_build(uint32_t f_ref, uint16_t carrier_ratio, uint8_t depth_pct,
		    uint16_t dead_ticks, uint32_t tick_hz, struct spwm_step *steps, size_t n);

/**
 * @brief Pack steps into OUTB words, then validate the words.
 *
 * Pads with low frames to a 16-tick multiple.
 *
 * @param steps          input
 * @param n              step count
 * @param min_dead_ticks dead time every step and every A/B change must honour
 * @param buf            output words
 * @param max_words      buf size
 *
 * @return words written, or -EINVAL on a bad step or failed validation, -ENOMEM if it
 *         doesn't fit
 */
int spwm_compile(const struct spwm_step *steps, size_t n, uint16_t min_dead_ticks,
		 uint32_t *buf, size_t max_words);

/**
 * @brief Check packed words as the FLPR will play them, looping.
 *
 * @retval 0 if no frame has A and B high, and every A->B or B->A change has at least
 *         min_dead_ticks low frames between, including across the buffer wrap
 * @retval -EINVAL otherwise
 */
int spwm_validate(const uint32_t *buf, size_t words, uint16_t min_dead_ticks);

#endif /* SPWM_PATTERN_H_ */
