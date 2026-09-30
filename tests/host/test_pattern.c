/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Host check for src/pattern.c:
 *   gcc -Wall -I src tests/host/test_pattern.c src/pattern.c -lm -o test_pattern && ./test_pattern
 */

#include <errno.h>
#include <stdio.h>

#include "pattern.h"

static int fails;

#define CHECK(name, got, want)                                                                     \
	do {                                                                                       \
		int g = (got);                                                                     \
		printf("%-44s got %4d want %4d  %s\n", name, g, want, g == (want) ? "ok" : "FAIL");  \
		fails += (g != (want));                                                            \
	} while (0)

int main(void)
{
	struct spwm_step steps[10];
	uint32_t buf[64];

	/* B3 settings: 200 kHz ref, ratio 10 (2 MHz carrier), 64 MHz tick, dead 2 */
	CHECK("sine_build 200k/10/100%/dead2/64M", spwm_sine_build(200000, 10, 100, 2, 64000000,
								     steps, 10), 0);
	printf("  period %u, on_ticks:", steps[0].period_ticks);
	for (int k = 0; k < 10; k++) {
		printf(" %c%u", "-AB"[steps[k].leg], steps[k].on_ticks);
	}
	printf("\n");
	CHECK("compile sine, words", spwm_compile(steps, 10, 2, buf, 64), 20);

	uint32_t both_high[1] = {0x3U << 10};
	CHECK("validate A and B high in one frame", spwm_validate(both_high, 1, 0), -EINVAL);

	/* A at tick 0, B at tick 2: 1 low tick between */
	uint32_t gap1[1] = {0x1U | (0x2U << 4)};
	CHECK("validate A->B with 1 low tick, min 2", spwm_validate(gap1, 1, 2), -EINVAL);

	/* A at tick 0, B at tick 3: 2 low ticks between */
	uint32_t gap2[1] = {0x1U | (0x2U << 6)};
	CHECK("validate A->B with 2 low ticks, min 2", spwm_validate(gap2, 1, 2), 0);

	/* B at tick 0, A at tick 15: back to back across the wrap */
	uint32_t wrap[1] = {0x2U | (0x1U << 30)};
	CHECK("validate A->B across wrap, 0 low ticks", spwm_validate(wrap, 1, 1), -EINVAL);

	struct spwm_step short_dead[2] = {
		{SPWM_LEG_A, 1, 14, 16},
		{SPWM_LEG_B, 1, 14, 16},
	};
	CHECK("compile step dead 1 < min 2", spwm_compile(short_dead, 2, 2, buf, 64), -EINVAL);

	struct spwm_step too_wide[1] = {{SPWM_LEG_A, 2, 13, 16}};
	CHECK("compile 2*dead + on > period", spwm_compile(too_wide, 1, 2, buf, 64), -EINVAL);

	struct spwm_step alt[4] = {
		{SPWM_LEG_A, 2, 12, 16},
		{SPWM_LEG_B, 2, 12, 16},
		{SPWM_LEG_A, 2, 12, 16},
		{SPWM_LEG_B, 2, 12, 16},
	};
	CHECK("compile alternating A/B, dead 2", spwm_compile(alt, 4, 2, buf, 64), 4);
	CHECK("  same buffer at min dead 5", spwm_validate(buf, 4, 5), -EINVAL);

	CHECK("compile into too small buffer", spwm_compile(steps, 10, 2, buf, 19), -ENOMEM);

	printf("%d failed\n", fails);
	return fails != 0;
}
