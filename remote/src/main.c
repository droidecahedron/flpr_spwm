/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 *  @brief FLPR side of flpr_spwm.
 *
 *  Scaffolding: answers every START doorbell with a DONE doorbell. The
 *  pattern streamer replaces the callback body in a later commit.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>

#include "spwm_ipc.h"

/* The FLPR links into zephyr,sram. spwm_shm has to sit above it. */
BUILD_ASSERT(SPWM_SHM_ADDR >= DT_REG_ADDR(DT_CHOSEN(zephyr_sram)) + DT_REG_SIZE(DT_CHOSEN(zephyr_sram)),
	     "spwm_shm overlaps FLPR code/data");

static const struct mbox_dt_spec rx = MBOX_DT_SPEC_GET(DT_PATH(doorbells), rx);
static const struct mbox_dt_spec tx = MBOX_DT_SPEC_GET(DT_PATH(doorbells), tx);

static void start_cb(const struct device *dev, mbox_channel_id_t channel_id, void *user_data,
		     struct mbox_msg *data)
{
	/* No console on the FLPR, so a failed send only shows as a missing DONE on the app */
	(void)mbox_send_dt(&tx, NULL);
}

int main(void)
{
	int err;

	err = mbox_register_callback_dt(&rx, start_cb, NULL);
	if (err) {
		return err;
	}

	err = mbox_set_enabled_dt(&rx, true);
	if (err) {
		return err;
	}

	while (1) {
		k_cpu_idle();
	}

	return 0;
}
