/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 *  @brief Control block shared by the app core and the FLPR.
 *
 *  Lives at the start of the spwm_shm DT region, which both images carve out of the
 *  top 32 KB of the nordic-flpr SRAM. Pattern buffers follow it in the same region.
 *  All offsets are bytes from the region base, so neither side needs the other's pointers.
 */

#ifndef SPWM_IPC_H_
#define SPWM_IPC_H_

#include <stdint.h>
#include <zephyr/devicetree.h>

#define SPWM_IPC_VERSION 1

#define SPWM_SHM_ADDR DT_REG_ADDR(DT_NODELABEL(spwm_shm))
#define SPWM_SHM_SIZE DT_REG_SIZE(DT_NODELABEL(spwm_shm))

/* Pattern buffers start here, 64 B keeps the control block on its own */
#define SPWM_SHM_BUF_OFF 64U
#define SPWM_SHM_BUF_MAX_WORDS ((SPWM_SHM_SIZE - SPWM_SHM_BUF_OFF) / sizeof(uint32_t))

/* One 32-bit OUTB word holds 16 two-bit frames (FRAMEWIDTH = 2, SHIFTCNTB = 15) */
#define SPWM_TICKS_PER_WORD 16U

enum spwm_state {
	SPWM_STATE_IDLE = 0,
	SPWM_STATE_ARMED,
	SPWM_STATE_RUNNING,
	SPWM_STATE_DONE,
	SPWM_STATE_ERROR,
};

enum spwm_err {
	SPWM_ERR_NONE = 0,
	SPWM_ERR_VERSION,
	SPWM_ERR_BUF,
};

struct spwm_ctrl {
	uint32_t version;        /* app: SPWM_IPC_VERSION, FLPR checks it at START */
	uint32_t state;          /* FLPR: enum spwm_state */
	uint32_t err;            /* FLPR: enum spwm_err when state is ERROR */
	uint32_t buf_off;        /* app: buffer to play */
	uint32_t buf_words;      /* app */
	uint32_t next_buf_off;   /* app: retune, taken at the next wrap. 0 = none */
	uint32_t next_buf_words; /* app */
	uint32_t loop_cnt;       /* app: buffer plays per START. 0 = until STOP */
	uint32_t cnttop;         /* app: VTIM CNT0 top, tick = 128 MHz / (cnttop + 1) (O2) */
	uint32_t stop_req;       /* app: checked at each wrap */
	uint32_t loops_done;     /* FLPR: progress */
};

BUILD_ASSERT(sizeof(struct spwm_ctrl) <= SPWM_SHM_BUF_OFF, "control block overlaps buffers");

#define SPWM_CTRL ((volatile struct spwm_ctrl *)SPWM_SHM_ADDR)
#define SPWM_BUF(off) ((volatile uint32_t *)(SPWM_SHM_ADDR + (off)))

#endif /* SPWM_IPC_H_ */
