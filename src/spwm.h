/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 *  @brief App core control of the FLPR streamer: START, STOP, DONE.
 */

#ifndef SPWM_H_
#define SPWM_H_

#include <stdint.h>
#include <zephyr/kernel.h>

/** @brief Start HFXO, set up the doorbells and the marker pin, reset the control block. */
int spwm_init(void);

/** @brief Load marker, P1.11 on nRF54L15DK. High while compiling and loading. */
void spwm_marker(bool on);

/**
 * @brief Write the control block and ring START.
 *
 * @param buf_off  byte offset of the words in spwm_shm
 * @param words    buffer length
 * @param cnttop   tick = 128 MHz / (cnttop + 1)
 * @param loop_cnt buffer plays, 0 = until spwm_stop()
 *
 * @retval -EBUSY if the FLPR is running
 */
int spwm_start(uint32_t buf_off, uint32_t words, uint16_t cnttop, uint32_t loop_cnt);

/**
 * @brief Queue another buffer. The FLPR switches to it at a wrap, one pass after it sees it.
 *
 * @retval -EBUSY if the previous retune or stop hasn't been taken yet
 */
int spwm_retune(uint32_t buf_off, uint32_t words);

/** @brief Set stop_req and ring STOP. The FLPR takes it at the next buffer wrap. */
int spwm_stop(void);

/**
 * @brief Wait for DONE.
 *
 * @param timeout    how long to wait
 * @param done_cyc   k_cycle_get_32() in the DONE ISR
 *
 * @retval -EAGAIN on timeout
 */
int spwm_wait_done(k_timeout_t timeout, uint32_t *done_cyc);

#endif /* SPWM_H_ */
