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

/** @brief Buffer layout for a sine at the closest reachable f_ref. */
struct spwm_sine_plan {
	uint32_t tick_hz;
	uint16_t carrier_ratio; /* carrier periods per ref period */
	uint32_t cycles;        /* M, ref cycles in one buffer */
	uint32_t ticks;         /* L, buffer length, a multiple of 16 */
	uint64_t f_mhz;         /* reached f_ref in mHz, tick_hz * M / L */
};

/**
 * @brief Pick the buffer that gets closest to f_ref.
 *
 * f_ref = tick_hz * M / L, L a multiple of 16 ticks. Searches every L up to max_ticks, so a
 * longer buffer gives a finer f_ref and a slower retune (one pass is L ticks). Carrier edges
 * land on the nearest tick, so carrier periods differ by at most one tick inside a buffer.
 *
 * @param f_ref         target, Hz
 * @param carrier_ratio carrier = carrier_ratio * f_ref
 * @param tick_hz       128 MHz / (cnttop + 1)
 * @param max_ticks     longest buffer allowed
 * @param max_steps     step array size, M * carrier_ratio must fit
 * @param plan          output
 *
 * @retval -EINVAL if nothing fits
 */
int spwm_sine_plan(uint32_t f_ref, uint16_t carrier_ratio, uint32_t tick_hz, uint32_t max_ticks,
		   size_t max_steps, struct spwm_sine_plan *plan);

/**
 * @brief Fill steps for a plan, natural-sampled at each carrier's center.
 *
 * Sign picks the leg, |sin| * depth scales the pulse between 0 and period - 2 * dead.
 *
 * @return steps written (M * carrier_ratio), or -EINVAL if a carrier can't hold the dead
 *         time, -ENOMEM if max_steps is too small
 */
int spwm_sine_build(const struct spwm_sine_plan *plan, uint8_t depth_pct, uint16_t dead_ticks,
		    struct spwm_step *steps, size_t max_steps);

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
