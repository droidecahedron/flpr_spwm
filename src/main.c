/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 *  @brief App core side of flpr_spwm.
 *
 *  Scaffolding: rings the FLPR START doorbell once a second and logs each
 *  DONE doorbell that comes back. Proves both images boot and the VEVIF
 *  path works in both directions.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>

#include "spwm_ipc.h"

LOG_MODULE_REGISTER(flpr_spwm, LOG_LEVEL_INF);

#define START_PERIOD_MS 1000

static const struct mbox_dt_spec rx = MBOX_DT_SPEC_GET(DT_PATH(doorbells), rx);
static const struct mbox_dt_spec tx = MBOX_DT_SPEC_GET(DT_PATH(doorbells), tx);

static atomic_t done_count;

static void done_cb(const struct device *dev, mbox_channel_id_t channel_id, void *user_data,
		    struct mbox_msg *data)
{
	atomic_inc(&done_count);
}

int main(void)
{
	int err;
	uint32_t sent = 0;

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
	SPWM_CTRL->version = SPWM_IPC_VERSION;
	LOG_INF("spwm_shm 0x%08x, %u B, %u buffer words", SPWM_SHM_ADDR, SPWM_SHM_SIZE,
		SPWM_SHM_BUF_MAX_WORDS);

	LOG_INF("flpr_spwm on %s, START on task %d, DONE on event %d", CONFIG_BOARD_TARGET,
		tx.channel_id, rx.channel_id);

	while (1) {
		err = mbox_send_dt(&tx, NULL);
		if (err) {
			LOG_ERR("mbox_send_dt failed (err %d)", err);
			return err;
		}
		sent++;

		k_msleep(START_PERIOD_MS);

		LOG_INF("START sent %u, DONE received %ld", sent, atomic_get(&done_count));
	}

	return 0;
}
