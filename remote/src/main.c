/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 *  @brief FLPR side of flpr_spwm.
 *
 *  Streams 2-bit frames out of VIO0/VIO1 through OUTB, paced by VTIM CNT0. START plays
 *  the buffer the control block points at, loop_cnt times or until stop_req, then rings
 *  DONE. Interrupts stay off from arm to end.
 *
 *  No instruction here toggles a pin per edge. The VIO shifter does: on every CNT0 tick
 *  (128 MHz / (cnttop + 1)) it moves the next 2 bits of the current 32-bit word onto
 *  P2.01 (bit 0, leg A) and P2.02 (bit 1, leg B), 16 ticks per word. The FLPR's only job
 *  is stream(): keep writing words into OUTB. Each write stalls until the shifter takes
 *  the previous word, so the loop runs exactly as fast as the pins need data. A late
 *  write shifts out zeros, so both legs go low.
 *
 *  | where             | what                                                      |
 *  | legs_init()       | pins to the FLPR (CTRLSEL = VPR), VIO0/VIO1 outputs, low  |
 *  | stream(), arm     | CNT0 top = cnttop, VIO in OUTB shift mode, 2 bits/tick    |
 *  | stream(), loop    | one csrw OUTB per word; wraps, loop count, stop, retune   |
 *  | stream(), end     | two zero words, counter stopped, shift mode off, pins low |
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

static const struct mbox_dt_spec rx = MBOX_DT_SPEC_GET(DT_PATH(doorbells), rx);
static const struct mbox_dt_spec tx = MBOX_DT_SPEC_GET(DT_PATH(doorbells), tx);
static const struct mbox_dt_spec stop = MBOX_DT_SPEC_GET(DT_PATH(doorbells), stop);

static volatile bool start_pending;

static void start_cb(const struct device *dev, mbox_channel_id_t channel_id, void *user_data,
		     struct mbox_msg *data)
{
	start_pending = true;
}

/* stop_req is read at the start of each pass with IRQs off, so the doorbell only has to
 * be taken
 */
static void stop_cb(const struct device *dev, mbox_channel_id_t channel_id, void *user_data,
		    struct mbox_msg *data)
{
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

/* Feeds the VIO shifter from the control block's buffer until the pattern is done: the
 * shifter moves the pins, this only keeps OUTB full. Arm order follows hrt_write() in
 * nrf/applications/hpf/mspi/src/hrt/hrt.c, with OUTB_TOGGLE swapped for plain OUTB.
 */
static void stream(volatile struct spwm_ctrl *ctrl)
{
	const volatile uint32_t *words = SPWM_BUF(ctrl->buf_off);
	uint32_t n = ctrl->buf_words;
	uint32_t loops = ctrl->loop_cnt;
	uint16_t cnttop = ctrl->cnttop;

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

	const volatile uint32_t *w = words;
	const volatile uint32_t *end = words + n;
	uint32_t played = 1; /* passes started, this one included */

	nrf_vpr_csr_vio_out_buffered_set(*w++);
	/* The counter doesn't start from 0, hrt.c "Temporary fix for max frequency" */
	nrf_vpr_csr_vtim_simple_counter_set(0, cnttop ? cnttop : 1);

	while (1) {
		/* Word 0 of this pass is queued, so this runs in slack. Decide now whether this
		 * pass is the last one, and which buffer the next pass plays.
		 */
		bool last = (played == loops);

		if (ctrl->pending) {
			ctrl->loops_done = played - 1;
			if (ctrl->stop_req) {
				last = true;
			}
			if (ctrl->next_buf_off) {
				words = SPWM_BUF(ctrl->next_buf_off);
				n = ctrl->next_buf_words;
				ctrl->next_buf_off = 0;
			}
			ctrl->pending = 0;
		}

		while (w != end) {
			/* Stalls until the shifter takes the previous word, which paces the loop */
			nrf_vpr_csr_vio_out_buffered_set(*w++);
		}

		/* Wrap. Nothing but register work before word 0 of the next pass goes out:
		 * at a 128 MHz tick a late write costs a whole zero word (O1).
		 */
		if (last) {
			break;
		}
		played++;
		w = words;
		end = words + n;
		nrf_vpr_csr_vio_out_buffered_set(*w++);
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

	ctrl->loops_done = played;
}

static bool buf_ok(uint32_t off, uint32_t words)
{
	return off >= SPWM_SHM_BUF_OFF && (off % sizeof(uint32_t)) == 0 && words > 0 &&
	       words <= (SPWM_SHM_SIZE - off) / sizeof(uint32_t);
}

static void run(volatile struct spwm_ctrl *ctrl)
{
	if (ctrl->version != SPWM_IPC_VERSION) {
		ctrl->err = SPWM_ERR_VERSION;
		ctrl->state = SPWM_STATE_ERROR;
		return;
	}

	if (!buf_ok(ctrl->buf_off, ctrl->buf_words) ||
	    (ctrl->next_buf_off && !buf_ok(ctrl->next_buf_off, ctrl->next_buf_words))) {
		ctrl->err = SPWM_ERR_BUF;
		ctrl->state = SPWM_STATE_ERROR;
		return;
	}

	ctrl->err = SPWM_ERR_NONE;
	ctrl->loops_done = 0;
	ctrl->pending = 0;
	ctrl->state = SPWM_STATE_RUNNING;
	stream(ctrl);
	ctrl->state = SPWM_STATE_DONE;
}

int main(void)
{
	int err;

	legs_init();

	err = mbox_register_callback_dt(&rx, start_cb, NULL);
	if (err) {
		return err;
	}

	err = mbox_set_enabled_dt(&rx, true);
	if (err) {
		return err;
	}

	err = mbox_register_callback_dt(&stop, stop_cb, NULL);
	if (err) {
		return err;
	}

	err = mbox_set_enabled_dt(&stop, true);
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

		run(SPWM_CTRL);

		irq_unlock(key);

		/* No console on the FLPR, so a failed send only shows as a missing DONE on the app */
		(void)mbox_send_dt(&tx, NULL);
	}

	return 0;
}
