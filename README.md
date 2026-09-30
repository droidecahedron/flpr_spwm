# flpr_spwm

Drives a software PWM through the FLPR, the small extra processor inside the nRF54L15.

## What it does
- The main processor works out when each of two output pins should turn on and off.
- It hands that plan to the FLPR and tells it to start.
- The FLPR switches the pins on its own, so the main processor is free for other work.
- When the FLPR is done, it tells the main processor.

> [!NOTE]
> Proof of concept. The main processor runs twelve fixed tests in a loop. There's no way to pick your own waveform yet without editing `src/main.c`.

## Requirements

### Hardware
- `nRF54L15 DK`
- `nRF54LM20 DK` also builds

### Software
- `nRF Connect SDK v3.4.0`

## Build
From an nRF Connect SDK v3.4.0 terminal, in this folder:

```
west build -b nrf54l15dk/nrf54l15/cpuapp
west flash
```

One build makes both programs, one for the main processor and one for the FLPR. `west flash` loads both.

To also send Bluetooth advertisements while the tests run:

```
west build -b nrf54l15dk/nrf54l15/cpuapp -- -DEXTRA_CONF_FILE=overlay-ble.conf
```

## What you should see
The DK's serial port prints one line per test, twelve tests, over and over:

```
b2_alt_64M: 4 words, cnttop 1, loop_cnt 1000, loops_done 1000, state 3, START->DONE 1029 us
b3_sine_64M: 20 words, cnttop 1, loop_cnt 2000, loops_done 2000, state 3, START->DONE 10008 us
b3_sine_128M: 40 words, cnttop 0, loop_cnt 2000, loops_done 2000, state 3, START->DONE 10010 us
b4_n100_64M: 20 words, cnttop 1, loop_cnt 100, loops_done 100, state 3, START->DONE 515 us
b4_stop_64M: 20 words, cnttop 1, loop_cnt 0, loops_done 401, state 3, START->DONE 2020 us
b6_retune_64M: 20 words, cnttop 1, loop_cnt 0, loops_done 249, state 3, START->DONE 2033 us
b2_alt_128M_d1: 4 words, cnttop 0, loop_cnt 2000, loops_done 2000, state 3, START->DONE 1014 us
b8_minpulse_128M: 16 words, cnttop 0, loop_cnt 1000, loops_done 1000, state 3, START->DONE 2022 us
b7_f200000: 40 words, cnttop 0, loop_cnt 4000, loops_done 4000, state 3, START->DONE 20030 us
b7_f187654: 810 words, cnttop 0, loop_cnt 197, loops_done 197, state 3, START->DONE 19971 us
b7_f187655: 7631 words, cnttop 0, loop_cnt 20, loops_done 20, state 3, START->DONE 19085 us
b7_f187664: 6096 words, cnttop 0, loop_cnt 26, loops_done 26, state 3, START->DONE 19834 us
```

The first six check the basics. `b2_alt_128M_d1` and `b8_minpulse_128M` push the shortest gap and pulse. The `b7_f*` tests ask for four nearby frequencies and play the closest the chip can make (the `plan` line before each one, not shown here).

`loops_done` should match `loop_cnt`. The last two tests run until they're told to stop, so they have no count to match. The last one also switches to a slightly lower frequency halfway through. `state 3` means the FLPR finished.

On a logic analyzer:

| pin | what it does |
| --- | --- |
| P2.01 | output A pulses |
| P2.02 | output B pulses. A and B are never high at the same time |
| P1.11 | high while the main processor builds a pattern, plus a short blip when the FLPR says it's done |

Run on an nRF54L15 DK. Not run on an nRF54LM20 DK.
