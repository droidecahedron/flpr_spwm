/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <math.h>
#include <string.h>

#include "pattern.h"

#define TICKS_PER_WORD 16 /* FRAMEWIDTH 2 in a 32-bit OUTB word */
#define FRAME_A 0x1U      /* VIO0 */
#define FRAME_B 0x2U      /* VIO1 */

#define PI_F 3.14159265f

static uint32_t frame_get(const uint32_t *buf, size_t tick)
{
	return (buf[tick / TICKS_PER_WORD] >> (2 * (tick % TICKS_PER_WORD))) & 0x3U;
}

int spwm_sine_build(uint32_t f_ref, uint16_t carrier_ratio, uint8_t depth_pct,
		    uint16_t dead_ticks, uint32_t tick_hz, struct spwm_step *steps, size_t n)
{
	if (f_ref == 0 || carrier_ratio == 0 || depth_pct > 100) {
		return -EINVAL;
	}

	uint64_t div = (uint64_t)f_ref * carrier_ratio;
	uint32_t period = (uint32_t)((tick_hz + div / 2) / div);

	if (period > UINT16_MAX || period <= 2U * dead_ticks) {
		return -EINVAL;
	}

	uint32_t span = period - 2U * dead_ticks;

	for (size_t k = 0; k < n; k++) {
		/* Mid-period sample, k counts carrier periods */
		float s = sinf(2.0f * PI_F * ((float)(k % carrier_ratio) + 0.5f) / carrier_ratio);
		uint32_t on = (uint32_t)lroundf(fabsf(s) * depth_pct * span / 100.0f);

		steps[k].period_ticks = period;
		steps[k].dead_ticks = dead_ticks;
		steps[k].on_ticks = on;
		steps[k].leg = (on == 0) ? SPWM_LEG_NONE : (s > 0.0f ? SPWM_LEG_A : SPWM_LEG_B);
	}

	return 0;
}

int spwm_validate(const uint32_t *buf, size_t words, uint16_t min_dead_ticks)
{
	size_t ticks = words * TICKS_PER_WORD;
	uint32_t last = 0;
	uint32_t low_run = 0;

	/* Two passes, so the A/B change across the wrap is checked with real history */
	for (size_t i = 0; i < 2 * ticks; i++) {
		uint32_t f = frame_get(buf, i % ticks);

		if (f == (FRAME_A | FRAME_B)) {
			return -EINVAL;
		}

		if (f == 0) {
			low_run++;
			continue;
		}

		if (last != 0 && last != f && low_run < min_dead_ticks) {
			return -EINVAL;
		}

		last = f;
		low_run = 0;
	}

	return 0;
}

int spwm_compile(const struct spwm_step *steps, size_t n, uint16_t min_dead_ticks,
		 uint32_t *buf, size_t max_words)
{
	size_t ticks = 0;

	for (size_t k = 0; k < n; k++) {
		const struct spwm_step *s = &steps[k];

		if (s->leg > SPWM_LEG_B || s->period_ticks == 0 || s->dead_ticks < min_dead_ticks ||
		    2U * s->dead_ticks + s->on_ticks > s->period_ticks) {
			return -EINVAL;
		}
		ticks += s->period_ticks;
	}

	size_t words = (ticks + TICKS_PER_WORD - 1) / TICKS_PER_WORD;

	if (words == 0 || words > max_words) {
		return -ENOMEM;
	}

	memset(buf, 0, words * sizeof(uint32_t));

	size_t t = 0;

	for (size_t k = 0; k < n; k++) {
		const struct spwm_step *s = &steps[k];
		uint32_t frame = (s->leg == SPWM_LEG_A) ? FRAME_A : FRAME_B;
		/* Centered pulse, odd leftover tick goes after it */
		size_t start = t + (s->period_ticks - s->on_ticks) / 2;

		if (s->leg != SPWM_LEG_NONE) {
			for (size_t i = start; i < start + s->on_ticks; i++) {
				buf[i / TICKS_PER_WORD] |= frame << (2 * (i % TICKS_PER_WORD));
			}
		}
		t += s->period_ticks;
	}

	if (spwm_validate(buf, words, min_dead_ticks)) {
		return -EINVAL;
	}

	return (int)words;
}
