/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Host check for src/pattern.c:
 *   gcc -Wall -I src tests/host/test_pattern.c src/pattern.c -lm -o test_pattern && ./test_pattern
 */

#include <errno.h>
#include <math.h>
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
	static struct spwm_step steps[4096];
	static uint32_t buf[8176];
	struct spwm_sine_plan plan;

	/* B3 settings: 200 kHz ref, ratio 10 (2 MHz carrier), 64 MHz tick, dead 2 */
	CHECK("plan 200k/10/64M, max 320 ticks", spwm_sine_plan(200000, 10, 64000000, 320, 4096,
								 &plan), 0);
	CHECK("  M", plan.cycles, 1);
	CHECK("  L", plan.ticks, 320);
	CHECK("sine_build steps", spwm_sine_build(&plan, 100, 2, steps, 4096), 10);
	printf("  period %u, on_ticks:", steps[0].period_ticks);
	for (int k = 0; k < 10; k++) {
		printf(" %c%u", "-AB"[steps[k].leg], steps[k].on_ticks);
	}
	printf("\n");
	CHECK("compile sine, words", spwm_compile(steps, 10, 2, buf, 64), 20);

	/* Granularity at 128 MHz with the whole spwm_shm buffer (8176 words) */
	static const uint32_t targets[] = {200000, 200001, 200010, 187654, 100000, 100001, 300001};

	for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
		int n;

		spwm_sine_plan(targets[i], 10, 128000000, 8176 * 16, 4096, &plan);
		n = spwm_sine_build(&plan, 100, 1, steps, 4096);
		printf("  %6u Hz -> M %4u L %6u (%4u words) f %u.%03u Hz err %+lld mHz, steps %d, "
		       "compile %d\n",
		       targets[i], plan.cycles, plan.ticks, plan.ticks / 16,
		       (unsigned)(plan.f_mhz / 1000), (unsigned)(plan.f_mhz % 1000),
		       (long long)plan.f_mhz - (long long)targets[i] * 1000, n,
		       n > 0 ? spwm_compile(steps, n, 1, buf, 8176) : n);
	}
	/* Worst case the planner can promise: the gap between neighbours next to a simple
	 * ratio like 128 MHz / 640 is f * 16 / L_max. Sweep 100-300 kHz in 7 Hz steps.
	 */
	{
		double worst = 0, sum = 0;
		int cnt = 0, over = 0;

		for (uint32_t f = 100000; f <= 300000; f += 7) {
			spwm_sine_plan(f, 10, 128000000, 8176 * 16, 4096, &plan);
			double err = fabs((double)plan.f_mhz / 1000.0 - f);
			double bound = (double)f * 16 / (8176 * 16);

			worst = err > worst ? err : worst;
			sum += err;
			cnt++;
			over += err > bound;
		}
		printf("  sweep 100-300 kHz, 128 MHz tick, 8176 words: %d targets, mean |err| %.3f Hz, "
		       "worst %.3f Hz\n", cnt, sum / cnt, worst);
		CHECK("sweep: |err| within f * 16 / L_max", over, 0);
	}
	CHECK("build with too few steps",
	      (spwm_sine_plan(187654, 10, 128000000, 8176 * 16, 4096, &plan),
	       spwm_sine_build(&plan, 100, 1, steps, 10)), -ENOMEM);

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

	spwm_sine_plan(200000, 10, 64000000, 320, 4096, &plan);
	spwm_sine_build(&plan, 100, 2, steps, 4096);
	CHECK("compile into too small buffer", spwm_compile(steps, 10, 2, buf, 19), -ENOMEM);

	printf("%d failed\n", fails);
	return fails != 0;
}
