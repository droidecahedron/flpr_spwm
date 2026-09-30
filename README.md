# flpr_spwm

Drives a software PWM through the FLPR, the small extra processor inside the nRF54L15.

## What it's for
Drives two pins with any on/off pattern you like, in 7.8 ns steps, for when the PWM peripheral doesn't do exactly what you want. The pattern plays on its own without the main processor touching every edge, and the two pins are never on at once, with a gap (dead time) you choose between them. The sine-shaped pulse train in `src/main.c` is one example of a pattern.

## What it does
- The main processor works out when each of two output pins should turn on and off.
- It hands that plan to the FLPR and tells it to start.
- The FLPR switches the pins on its own, so the main processor is free for other work.
- When the FLPR is done, it tells the main processor.

The frequency can only be `tick x M / L`: M sine cycles in a buffer of L ticks, where L is a multiple of 16. The planner tries every L up to your cap and keeps the closest match, so a longer cap gives a finer step. Every pulse width and dead time is a whole number of ticks, 7.8 ns at 128 MHz, the finest this chip can do. The cost of a big buffer is a slower frequency change (up to about 2 ms) and a longer build (up to about 80 ms).

```
 you: frequency, carrier ratio, depth, dead time, tick, buffer cap
   |
   v
 main processor   spwm_sine_plan()   closest frequency the chip can make
                  spwm_sine_build()  one pulse per carrier period
                  spwm_compile()     packs 2 bits per tick, rejects A+B on or short dead time
   |  writes the words and a small control block
   v
 shared RAM (32 KB)
   |  START doorbell
   v
 FLPR             reads a word, hands it to the pin shifter, repeats
   |  one 2-bit step per tick
   v
 P2.01 pin A / P2.02 pin B  ->  some strange waveform requiring hardware
                                   ^ you can put a scope here
   |
 FLPR  --DONE doorbell-->  main processor
```

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

## Using it
You pass in the frequency, how many carrier pulses per sine cycle, the depth, the dead time, the tick rate, and the longest buffer you'll accept (which trades frequency step against how fast you can change frequency).

```c
struct spwm_sine_plan plan;

/* 1. Pick the closest frequency the chip can make */
spwm_sine_plan(187654,          /* f_ref, Hz */
               10,              /* carrier = 10 x f_ref */
               128000000,       /* tick, 128 MHz / (cnttop + 1) */
               8176 * 16,       /* longest buffer, in ticks */
               MAX_STEPS, &plan);
/* plan.f_mhz = 187654321, the frequency it will really play, in mHz */

/* 2. Make the pulses: one per carrier period, width = |sin| x depth */
int n = spwm_sine_build(&plan, 100 /* depth % */, 1 /* dead ticks */, steps, MAX_STEPS);

/* 3. Pack into the shared buffer, rejects A+B high or short dead time */
int words = spwm_compile(steps, n, 1, (uint32_t *)SPWM_BUF(off), max_words);

/* 4. Play it. The FLPR runs it with no CPU work */
spwm_start(off, words, 0 /* cnttop */, 0 /* loop_cnt, 0 = forever */);
spwm_retune(off2, words2);   /* switch at the next buffer end, no gap */
spwm_stop();                 /* stops after the current pass */
spwm_wait_done(K_SECONDS(1), NULL);
```

`src/main.c` has working examples of each call.

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

`loops_done` should match `loop_cnt`. `b4_stop_64M` and `b6_retune_64M` run until they're told to stop, so they have no count to match. `b6_retune_64M` also switches to a slightly lower frequency halfway through. `state 3` means the FLPR finished.

On a logic analyzer:

| pin | what it does |
| --- | --- |
| P2.01 | output A pulses |
| P2.02 | output B pulses. A and B are never high at the same time |
| P1.11 | high while the main processor builds a pattern, plus a short blip when the FLPR says it's done |

Run on an nRF54L15 DK. Not run on an nRF54LM20 DK.

## Testing and measurement
- Host test, no hardware: `gcc -I src tests/host/test_pattern.c src/pattern.c -lm -o test_pattern && ./test_pattern`. It sweeps 100-300 kHz and checks every plan lands within the worst-case step, and checks the compiler rejects A+B on and short dead time.
- On the DK, the main processor plays each test in a loop. A Saleae Logic Pro 8 in Logic 2 records P1.11, P2.01 and P2.02 at 500 MS/s.
- Frequency: a straight-line fit over about 3700 sine cycles, calibrated against an exact 200 kHz test. It matched the plan to within 3 mHz.
- Pulses and gaps: every pulse width, dead time and buffer-to-buffer spacing is compared, tick by tick, against what was programmed. So far no sample had A and B both on, and no words were lost.

| measured at 128 MHz | result |
| --- | --- |
| shortest pulse seen | 2 ticks (15.6 ns), reads 2-4 ns longer. 1 tick wasn't seen by the analyzer |
| shortest dead time | 1 tick each side, reads 10-14 ns between the pins |
| frequency vs plan | within 3 mHz |
