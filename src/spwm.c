/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/logging/log.h>
#include <soc.h>
#include <hal/nrf_gpio.h>

#include "spwm.h"
#include "spwm_ipc.h"

LOG_MODULE_REGISTER(spwm, LOG_LEVEL_INF);

#define MARKER_PIN NRF_DT_GPIOS_TO_PSEL(DT_PATH(zephyr_user), marker_gpios)

static const struct gpio_dt_spec marker = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), marker_gpios);

static const struct mbox_dt_spec rx = MBOX_DT_SPEC_GET(DT_PATH(doorbells), rx);
static const struct mbox_dt_spec tx = MBOX_DT_SPEC_GET(DT_PATH(doorbells), tx);
static const struct mbox_dt_spec stop = MBOX_DT_SPEC_GET(DT_PATH(doorbells), stop);

static K_SEM_DEFINE(done_sem, 0, 1);
static volatile uint32_t done_cyc_isr;

void spwm_marker(bool on)
{
	/* Straight to the port, gpio_pin_set_dt() adds a few hundred ns of API on the marker */
	if (on) {
		nrf_gpio_pin_set(MARKER_PIN);
	} else {
		nrf_gpio_pin_clear(MARKER_PIN);
	}
}

/* VPR00 IRQ 76. The marker pulse puts DONE on the same Logic channel as the load marker. */
static void done_cb(const struct device *dev, mbox_channel_id_t channel_id, void *user_data,
		    struct mbox_msg *data)
{
	nrf_gpio_pin_set(MARKER_PIN);
	nrf_gpio_pin_clear(MARKER_PIN);
	/* After the pulse: a GRTC read took 0.9-30 us here on the bench */
	done_cyc_isr = k_cycle_get_32();
	k_sem_give(&done_sem);
}

static int hfxo_start(void)
{
	struct onoff_manager *mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	struct onoff_client cli;
	int res;
	int err;

	sys_notify_init_spinwait(&cli.notify);
	err = onoff_request(mgr, &cli);
	if (err < 0) {
		return err;
	}

	do {
		err = sys_notify_fetch_result(&cli.notify, &res);
	} while (err == -EAGAIN);

	return err ? err : res;
}

int spwm_init(void)
{
	int err;

	/* HFINT alone put the tick +587 to +1275 ppm off, HFXO +8 ppm (B1) */
	err = hfxo_start();
	if (err) {
		LOG_ERR("HFXO start failed (err %d)", err);
		return err;
	}

	err = gpio_pin_configure_dt(&marker, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("marker configure failed (err %d)", err);
		return err;
	}

	err = mbox_register_callback_dt(&rx, done_cb, NULL);
	if (err) {
		LOG_ERR("mbox_register_callback_dt failed (err %d)", err);
		return err;
	}

	err = mbox_set_enabled_dt(&rx, true);
	if (err) {
		LOG_ERR("mbox_set_enabled_dt failed (err %d)", err);
		return err;
	}

	/* nordic_vpr_launcher has copied the FLPR image by now, so this sticks */
	SPWM_CTRL->state = SPWM_STATE_IDLE;
	SPWM_CTRL->err = SPWM_ERR_NONE;
	SPWM_CTRL->next_buf_off = 0;
	SPWM_CTRL->stop_req = 0;
	SPWM_CTRL->version = SPWM_IPC_VERSION;

	return 0;
}

int spwm_start(uint32_t buf_off, uint32_t words, uint16_t cnttop, uint32_t loop_cnt)
{
	volatile struct spwm_ctrl *ctrl = SPWM_CTRL;

	if (ctrl->state == SPWM_STATE_RUNNING) {
		return -EBUSY;
	}

	k_sem_reset(&done_sem);

	ctrl->buf_off = buf_off;
	ctrl->buf_words = words;
	ctrl->next_buf_off = 0;
	ctrl->loop_cnt = loop_cnt;
	ctrl->cnttop = cnttop;
	ctrl->stop_req = 0;
	ctrl->state = SPWM_STATE_ARMED;

	return mbox_send_dt(&tx, NULL);
}

int spwm_stop(void)
{
	SPWM_CTRL->stop_req = 1;

	return mbox_send_dt(&stop, NULL);
}

int spwm_wait_done(k_timeout_t timeout, uint32_t *done_cyc)
{
	int err = k_sem_take(&done_sem, timeout);

	if (!err && done_cyc) {
		*done_cyc = done_cyc_isr;
	}

	return err;
}
