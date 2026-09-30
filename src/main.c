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
#include <zephyr/bluetooth/bluetooth.h>

#include "pattern.h"
#include "spwm.h"
#include "spwm_ipc.h"

LOG_MODULE_REGISTER(flpr_spwm, LOG_LEVEL_INF);

#define GAP_MS 200
#define MAX_STEPS 4096
#define BUF_OFF SPWM_SHM_BUF_OFF
#define REF_HZ 200000
#define RATIO 10 /* carrier = 10 * ref, 2 MHz at 200 kHz */

/* loop_cnt that plays about 20 ms, whatever the buffer length */
#define LOOPS_20MS UINT32_MAX

enum shape {
	SHAPE_ALT, /* A, B, A, B at the minimum dead time, worst case for A/B changes */
	SHAPE_SINE,
	SHAPE_RETUNE,   /* sine at f_ref, then retune_hz from a second buffer, then STOP */
	SHAPE_MINPULSE, /* A only, on_ticks 1..8 in 32-tick periods */
};

struct bench_case {
	const char *name;
	enum shape shape;
	uint16_t cnttop;
	uint16_t dead_ticks;
	uint32_t loop_cnt; /* 0: run until spwm_stop() after stop_after_us */
	uint32_t stop_after_us;
	uint32_t f_ref;     /* 0: REF_HZ */
	uint32_t max_words; /* planner buffer cap, 0: one ref cycle */
	uint32_t retune_hz;
};

static const struct bench_case cases[] = {
	{"b2_alt_64M", SHAPE_ALT, 1, 2, 1000},
	{"b3_sine_64M", SHAPE_SINE, 1, 2, 2000},
	{"b3_sine_128M", SHAPE_SINE, 0, 4, 2000},
	{"b4_n100_64M", SHAPE_SINE, 1, 2, 100},
	{"b4_stop_64M", SHAPE_SINE, 1, 2, 0, 2000},
	{"b6_retune_64M", SHAPE_RETUNE, 1, 2, 0, 1000, 0, 0, 188235},
	/* Limits at 128 MHz: 1-tick dead time, pulse floor, fine frequency steps */
	{"b2_alt_128M_d1", SHAPE_ALT, 0, 1, 2000},
	{"b8_minpulse_128M", SHAPE_MINPULSE, 0, 4, 1000},
	{"b7_f200000", SHAPE_SINE, 0, 1, LOOPS_20MS, 0, 200000, SPWM_SHM_BUF_MAX_WORDS},
	{"b7_f187654", SHAPE_SINE, 0, 1, LOOPS_20MS, 0, 187654, SPWM_SHM_BUF_MAX_WORDS},
	{"b7_f187655", SHAPE_SINE, 0, 1, LOOPS_20MS, 0, 187655, SPWM_SHM_BUF_MAX_WORDS},
	{"b7_f187664", SHAPE_SINE, 0, 1, LOOPS_20MS, 0, 187664, SPWM_SHM_BUF_MAX_WORDS},
};

static struct spwm_step steps[MAX_STEPS];
static struct spwm_sine_plan plan;

#if defined(CONFIG_FLPR_SPWM_BLE)
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* 20 ms, the shortest advertising interval (BT Core v5.x, Vol 6, Part B, 4.4.2.2.1) */
#define ADV_INT_20MS 0x20

static int ble_start(void)
{
	const struct bt_le_adv_param param =
		BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_USE_IDENTITY, ADV_INT_20MS, ADV_INT_20MS, NULL);
	int err;

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable failed (err %d)", err);
		return err;
	}

	err = bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOG_ERR("bt_le_adv_start failed (err %d)", err);
		return err;
	}

	LOG_INF("advertising as %s every 20 ms", CONFIG_BT_DEVICE_NAME);

	return 0;
}
#else
static int ble_start(void)
{
	return -ENOTSUP;
}
#endif /* CONFIG_FLPR_SPWM_BLE */

static uint32_t tick_hz_of(const struct bench_case *c)
{
	return 128000000U / (c->cnttop + 1U);
}

/* Plans and compiles a sine into spwm_shm at off. max_words 0 = one ref cycle, as before. */
static int build_sine(const struct bench_case *c, uint32_t f_ref, uint32_t max_words,
		      uint32_t off)
{
	uint32_t room = SPWM_SHM_BUF_MAX_WORDS - (off - BUF_OFF) / sizeof(uint32_t);
	uint32_t tick_hz = tick_hz_of(c);
	uint32_t max_ticks;
	int n;
	int err;

	if (max_words == 0) {
		/* One ref cycle, rounded to whole words */
		max_ticks = ROUND_UP(tick_hz / f_ref, SPWM_TICKS_PER_WORD);
	} else {
		max_ticks = MIN(max_words, room) * SPWM_TICKS_PER_WORD;
	}

	err = spwm_sine_plan(f_ref, RATIO, tick_hz, max_ticks, MAX_STEPS, &plan);
	if (err) {
		return err;
	}

	n = spwm_sine_build(&plan, 100, c->dead_ticks, steps, MAX_STEPS);
	if (n < 0) {
		return n;
	}

	return spwm_compile(steps, n, c->dead_ticks, (uint32_t *)SPWM_BUF(off), room);
}

static int build(const struct bench_case *c)
{
	size_t n;

	switch (c->shape) {
	case SHAPE_ALT:
		/* period 16, pulse fills it but for dead_ticks each side */
		n = 4;
		for (size_t k = 0; k < n; k++) {
			steps[k] = (struct spwm_step){
				.leg = (k & 1) ? SPWM_LEG_B : SPWM_LEG_A,
				.dead_ticks = c->dead_ticks,
				.on_ticks = 16 - 2 * c->dead_ticks,
				.period_ticks = 16,
			};
		}
		break;
	case SHAPE_MINPULSE:
		n = 8;
		for (size_t k = 0; k < n; k++) {
			steps[k] = (struct spwm_step){
				.leg = SPWM_LEG_A,
				.dead_ticks = c->dead_ticks,
				.on_ticks = k + 1,
				.period_ticks = 32,
			};
		}
		break;
	default:
		return build_sine(c, c->f_ref ? c->f_ref : REF_HZ, c->max_words, BUF_OFF);
	}

	return spwm_compile(steps, n, c->dead_ticks, (uint32_t *)SPWM_BUF(BUF_OFF),
			    SPWM_SHM_BUF_MAX_WORDS);
}

static int run_case(const struct bench_case *c)
{
	uint32_t t_start, t_done;
	uint32_t retune_off = 0;
	int retune_words = 0;
	int words;
	int err;

	spwm_marker(true);
	words = build(c);
	if (c->shape == SHAPE_RETUNE && words > 0) {
		retune_off = BUF_OFF + words * sizeof(uint32_t);
		retune_words = build_sine(c, c->retune_hz, 85, retune_off);
	}
	spwm_marker(false);
	if (retune_words < 0) {
		LOG_ERR("%s: retune compile failed (err %d)", c->name, retune_words);
		return retune_words;
	}
	if (words < 0) {
		LOG_ERR("%s: compile failed (err %d)", c->name, words);
		return words;
	}

	uint32_t loop_cnt = c->loop_cnt;

	if (loop_cnt == LOOPS_20MS) {
		/* words * 16 ticks per pass */
		loop_cnt = MAX(1U, (uint32_t)((uint64_t)tick_hz_of(c) / 50U /
					      ((uint64_t)words * SPWM_TICKS_PER_WORD)));
	}

	if (c->shape == SHAPE_SINE && c->max_words) {
		LOG_INF("%s: plan M %u L %u ticks, f %u.%03u Hz", c->name, plan.cycles, plan.ticks,
			(uint32_t)(plan.f_mhz / 1000U), (uint32_t)(plan.f_mhz % 1000U));
	}

	t_start = k_cycle_get_32();
	err = spwm_start(BUF_OFF, words, c->cnttop, loop_cnt);
	if (err) {
		LOG_ERR("%s: spwm_start failed (err %d)", c->name, err);
		return err;
	}

	if (c->shape == SHAPE_RETUNE) {
		k_busy_wait(c->stop_after_us);
		err = spwm_retune(retune_off, retune_words);
		if (err) {
			LOG_ERR("%s: spwm_retune failed (err %d)", c->name, err);
			return err;
		}
	}

	if (loop_cnt == 0) {
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
		c->name, words, c->cnttop, loop_cnt, SPWM_CTRL->loops_done, SPWM_CTRL->state,
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

	if (IS_ENABLED(CONFIG_FLPR_SPWM_BLE)) {
		err = ble_start();
		if (err) {
			return err;
		}
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
