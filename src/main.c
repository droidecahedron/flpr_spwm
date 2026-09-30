/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 *  @brief App core side of flpr_spwm.
 *
 *  Bench sequence, repeated: compile a pattern with the load marker high, START it, wait
 *  for DONE, log the result. One case per bench milestone, see cases[].
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "pattern.h"
#include "spwm.h"
#include "spwm_ipc.h"

LOG_MODULE_REGISTER(flpr_spwm, LOG_LEVEL_INF);

#define GAP_MS 200
#define MAX_STEPS 64
#define BUF_OFF SPWM_SHM_BUF_OFF

enum shape {
	SHAPE_ALT, /* A, B, A, B at the minimum dead time, worst case for A/B changes */
	SHAPE_SINE,
};

struct bench_case {
	const char *name;
	enum shape shape;
	uint16_t cnttop;
	uint16_t dead_ticks;
	uint32_t loop_cnt; /* 0: run until spwm_stop() after stop_after_us */
	uint32_t stop_after_us;
};

/* 200 kHz ref, ratio 10 = 2 MHz carrier. Dead time 31.25 ns in every case. */
static const struct bench_case cases[] = {
	{"b2_alt_64M", SHAPE_ALT, 1, 2, 1000, 0},
	{"b3_sine_64M", SHAPE_SINE, 1, 2, 2000, 0},
	{"b3_sine_128M", SHAPE_SINE, 0, 4, 2000, 0},
	{"b4_n100_64M", SHAPE_SINE, 1, 2, 100, 0},
	{"b4_stop_64M", SHAPE_SINE, 1, 2, 0, 2000},
};

static struct spwm_step steps[MAX_STEPS];

static int build(const struct bench_case *c)
{
	uint32_t tick_hz = 128000000U / (c->cnttop + 1U);
	size_t n;
	int err;

	if (c->shape == SHAPE_ALT) {
		/* period 16, pulse 12, 2 dead ticks each side */
		n = 4;
		for (size_t k = 0; k < n; k++) {
			steps[k] = (struct spwm_step){
				.leg = (k & 1) ? SPWM_LEG_B : SPWM_LEG_A,
				.dead_ticks = c->dead_ticks,
				.on_ticks = 16 - 2 * c->dead_ticks,
				.period_ticks = 16,
			};
		}
	} else {
		n = 10;
		err = spwm_sine_build(200000, 10, 100, c->dead_ticks, tick_hz, steps, n);
		if (err) {
			return err;
		}
	}

	return spwm_compile(steps, n, c->dead_ticks, (uint32_t *)SPWM_BUF(BUF_OFF),
			    SPWM_SHM_BUF_MAX_WORDS);
}

static int run_case(const struct bench_case *c)
{
	uint32_t t_start, t_done;
	int words;
	int err;

	spwm_marker(true);
	words = build(c);
	spwm_marker(false);
	if (words < 0) {
		LOG_ERR("%s: compile failed (err %d)", c->name, words);
		return words;
	}

	t_start = k_cycle_get_32();
	err = spwm_start(BUF_OFF, words, c->cnttop, c->loop_cnt);
	if (err) {
		LOG_ERR("%s: spwm_start failed (err %d)", c->name, err);
		return err;
	}

	if (c->loop_cnt == 0) {
		k_busy_wait(c->stop_after_us);
		err = spwm_stop();
		if (err) {
			LOG_ERR("%s: spwm_stop failed (err %d)", c->name, err);
			return err;
		}
	}

	err = spwm_wait_done(K_SECONDS(1), &t_done);
	if (err) {
		LOG_ERR("%s: no DONE, state %u", c->name, SPWM_CTRL->state);
		return err;
	}

	LOG_INF("%s: %d words, cnttop %u, loop_cnt %u, loops_done %u, state %u, "
		"START->DONE %u us",
		c->name, words, c->cnttop, c->loop_cnt, SPWM_CTRL->loops_done, SPWM_CTRL->state,
		k_cyc_to_us_floor32(t_done - t_start));

	return 0;
}

int main(void)
{
	int err;

	err = spwm_init();
	if (err) {
		return err;
	}

	LOG_INF("flpr_spwm on %s, spwm_shm 0x%08x, %u buffer words", CONFIG_BOARD_TARGET,
		SPWM_SHM_ADDR, SPWM_SHM_BUF_MAX_WORDS);

	while (1) {
		for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
			(void)run_case(&cases[i]);
			k_msleep(GAP_MS);
		}
	}

	return 0;
}
