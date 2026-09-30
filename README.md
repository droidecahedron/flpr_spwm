# flpr_spwm

Drives a software PWM through the FLPR, the small extra processor inside the nRF54L15.

## What it does
- The main processor works out when each of two output pins should turn on and off.
- It hands that plan to the FLPR and tells it to start.
- The FLPR switches the pins on its own, so the main processor is free for other work.
- When the FLPR is done, it tells the main processor.

> [!NOTE]
> Work in progress. Right now only the "start" and "done" messages exist. The main processor sends "start" once a second and counts the "done" replies. No pins move yet.

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

## What you should see
The DK's serial port prints one line per test, five tests, over and over:

```
b2_alt_64M: 4 words, cnttop 1, loop_cnt 1000, loops_done 1000, state 3, START->DONE 1029 us
b3_sine_64M: 20 words, cnttop 1, loop_cnt 2000, loops_done 2000, state 3, START->DONE 10020 us
b3_sine_128M: 40 words, cnttop 0, loop_cnt 2000, loops_done 2000, state 3, START->DONE 10276 us
b4_n100_64M: 20 words, cnttop 1, loop_cnt 100, loops_done 100, state 3, START->DONE 526 us
b4_stop_64M: 20 words, cnttop 1, loop_cnt 0, loops_done 401, state 3, START->DONE 2020 us
```

`loops_done` should match `loop_cnt`. The last test runs until it's told to stop, so it has no count to match. `state 3` means the FLPR finished.

On a logic analyzer:

| pin | what it does |
| --- | --- |
| P2.01 | output A pulses |
| P2.02 | output B pulses. A and B are never high at the same time |
| P1.11 | high while the main processor builds a pattern, plus a short blip when the FLPR says it's done |

Run on an nRF54L15 DK. Not run on an nRF54LM20 DK.
