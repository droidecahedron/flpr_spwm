/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 *  @brief FLPR side of flpr_spwm.
 *
 *  Streams 2-bit frames out of VIO0/VIO1 through OUTB, paced by VTIM CNT0. Each START
 *  doorbell plays a fixed test pattern at the next CNTTOP from cnttop_sweep[], then
 *  rings DONE. Interrupts stay off from arm to end.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <soc.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_vpr_csr_vio.h>
#include <hal/nrf_vpr_csr_vtim.h>

#include "spwm_ipc.h"

/* The FLPR links into zephyr,sram. spwm_shm has to sit above it. */
BUILD_ASSERT(SPWM_SHM_ADDR >= DT_REG_ADDR(DT_CHOSEN(zephyr_sram)) + DT_REG_SIZE(DT_CHOSEN(zephyr_sram)),
	     "spwm_shm overlaps FLPR code/data");

#define LEG_A_PIN NRF_DT_GPIOS_TO_PSEL_BY_IDX(DT_PATH(zephyr_user), leg_gpios, 0)
#define LEG_B_PIN NRF_DT_GPIOS_TO_PSEL_BY_IDX(DT_PATH(zephyr_user), leg_gpios, 1)

/* No OUTMODE.SEL on nRF54L15, a 2-bit frame always lands on VIO0/VIO1 (nrf54l15_types.h) */
BUILD_ASSERT(LEG_A_PIN == NRF_PIN_PORT_TO_PIN_NUMBER(1, 2), "leg A must be P2.01 (VIO0)");
BUILD_ASSERT(LEG_B_PIN == NRF_PIN_PORT_TO_PIN_NUMBER(2, 2), "leg B must be P2.02 (VIO1)");

#define VIO_LEGS (BIT(0) | BIT(1))

#define FRAME_WIDTH 2
/* SHIFTCNTB takes n - 1 for n shifts per word, hrt.c SHIFTCNTB_VALUE() */
#define SHIFTCNTB (SPWM_TICKS_PER_WORD - 1)

/* Test pattern, frames LSB first, frame bits [1:0] = {B, A}:
 * ticks 0-3 A high, 4-7 low, 8-11 B high, 12-15 low. A period = 16 ticks.
 */
#define TEST_WORD 0x00AA0055U
#define TEST_WORDS 256
#define TEST_LOOPS 4

/* One CNTTOP per START, cycled */
static const uint16_t cnttop_sweep[] = {0, 1, 3, 7, 15, 63};

static uint32_t test_buf[TEST_WORDS];

static const struct mbox_dt_spec rx = MBOX_DT_SPEC_GET(DT_PATH(doorbells), rx);
static const struct mbox_dt_spec tx = MBOX_DT_SPEC_GET(DT_PATH(doorbells), tx);

static volatile bool start_pending;

static void start_cb(const struct device *dev, mbox_channel_id_t channel_id, void *user_data,
		     struct mbox_msg *data)
{
	start_pending = true;
}

static void legs_init(void)
{
	nrf_vpr_csr_vio_out_set(0);
	nrf_vpr_csr_vio_dir_set(VIO_LEGS);

	/* CTRLSEL before anything drives the pins, PS GPIO > Peripheral and subsystem assignment */
	nrf_gpio_cfg(LEG_A_PIN, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT,
		     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_H0H1, NRF_GPIO_PIN_NOSENSE);
	nrf_gpio_cfg(LEG_B_PIN, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT,
		     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_H0H1, NRF_GPIO_PIN_NOSENSE);
	nrf_gpio_pin_control_select(LEG_A_PIN, NRF_GPIO_PIN_SEL_VPR);
	nrf_gpio_pin_control_select(LEG_B_PIN, NRF_GPIO_PIN_SEL_VPR);
}

/* Plays words[] loops times. Order follows hrt_write() in
 * nrf/applications/hpf/mspi/src/hrt/hrt.c, with OUTB_TOGGLE swapped for plain OUTB.
 */
static void stream(uint16_t cnttop, const volatile uint32_t *words, uint32_t n, uint32_t loops)
{
	const nrf_vpr_csr_vio_mode_out_t out_mode = {
		.mode = NRF_VPR_CSR_VIO_SHIFT_OUTB,
		.frame_width = FRAME_WIDTH,
	};
	const nrf_vpr_csr_vio_shift_ctrl_t shift_ctrl = {
		.shift_count = SHIFTCNTB,
		.out_mode = NRF_VPR_CSR_VIO_SHIFT_OUTB,
		.frame_width = FRAME_WIDTH,
		.in_mode = NRF_VPR_CSR_VIO_MODE_IN_CONTINUOUS,
	};

	nrf_vpr_csr_vtim_count_mode_set(0, NRF_VPR_CSR_VTIM_COUNT_RELOAD);
	nrf_vpr_csr_vtim_simple_counter_top_set(0, cnttop);
	nrf_vpr_csr_vio_mode_in_set(NRF_VPR_CSR_VIO_MODE_IN_CONTINUOUS);
	nrf_vpr_csr_vio_mode_out_set(&out_mode);
	/* n - 1 here too. hrt.c writes n, and on the bench that made word 0 17 ticks long:
	 * the 17th frame is the all-zero value after 16 shifts (B1).
	 */
	nrf_vpr_csr_vio_shift_cnt_out_set(SHIFTCNTB);
	nrf_vpr_csr_vio_shift_ctrl_buffered_set(&shift_ctrl);

	nrf_vpr_csr_vio_out_buffered_set(words[0]);
	/* The counter doesn't start from 0, hrt.c "Temporary fix for max frequency" */
	nrf_vpr_csr_vtim_simple_counter_set(0, cnttop ? cnttop : 1);

	for (uint32_t i = 1; i < n; i++) {
		/* Stalls until the shifter takes the previous word, which paces the loop */
		nrf_vpr_csr_vio_out_buffered_set(words[i]);
	}

	for (uint32_t l = 1; l < loops; l++) {
		for (uint32_t i = 0; i < n; i++) {
			nrf_vpr_csr_vio_out_buffered_set(words[i]);
		}
	}

	/* First zero write returns once the last real word is in the shifter, the second once
	 * that word has shifted out. Legs read 0 from here on.
	 */
	nrf_vpr_csr_vio_out_buffered_set(0);
	nrf_vpr_csr_vio_out_buffered_set(0);

	nrf_vpr_csr_vtim_count_mode_set(0, NRF_VPR_CSR_VTIM_COUNT_STOP);
	nrf_vpr_csr_vtim_simple_counter_set(0, 0);

	const nrf_vpr_csr_vio_mode_out_t idle_mode = {.mode = NRF_VPR_CSR_VIO_SHIFT_NONE};

	nrf_vpr_csr_vio_mode_out_set(&idle_mode);
	nrf_vpr_csr_vio_out_set(0);
}

int main(void)
{
	uint32_t run = 0;
	int err;

	legs_init();

	for (int i = 0; i < TEST_WORDS; i++) {
		test_buf[i] = TEST_WORD;
	}

	err = mbox_register_callback_dt(&rx, start_cb, NULL);
	if (err) {
		return err;
	}

	err = mbox_set_enabled_dt(&rx, true);
	if (err) {
		return err;
	}

	while (1) {
		unsigned int key = irq_lock();

		if (!start_pending) {
			k_cpu_atomic_idle(key);
			continue;
		}
		start_pending = false;

		stream(cnttop_sweep[run % ARRAY_SIZE(cnttop_sweep)], test_buf, TEST_WORDS, TEST_LOOPS);
		run++;

		irq_unlock(key);

		/* No console on the FLPR, so a failed send only shows as a missing DONE on the app */
		(void)mbox_send_dt(&tx, NULL);
	}

	return 0;
}
