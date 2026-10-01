# Limit cases, 128 MHz tick

Open `limits_sequence.sal` in Logic 2. It was captured on an nRF54L15 DK running build `482e4cb`, with a Saleae Logic Pro 8 at 500 MS/s and the 1.8 V setting. Channels: 0 = P1.11 (marker), 1 = P2.01 (pin A), 2 = P2.02 (pin B). The test sequence repeats every ~2.7 s. Times are from the start of the capture.

Put the view start at "go to" and the first pulse of the case is the first thing on screen. Logic 2 timing markers snap to the nearest edge, so drop them at the d1/d2 times.

| case | go to | zoom | marker d1 | marker d2 | should read | what it shows |
| --- | --- | --- | --- | --- | --- | --- |
| Shortest dead time | 0.842657388 s | 300 ns | A fall, 0.842657500 s | B rise, 0.842657514 s | 14 ns | 1 dead tick each side (15.6 ns programmed). A pulse 14 ticks (~114 ns). A and B never on together |
| Shortest pulse | 1.043576478 s | 2.4 us | | | widths 16, 26, 34, 42, 50, 58, 66 ns | pulses of 1-8 ticks, one per 250 ns slot. The 1-tick (7.8 ns) slot at ~1.043876228 s shows nothing on the analyzer. The rest step ~8 ns (one tick) each, starting with 2 ticks at 1.043876478 s |
| Frequency reference, 200 kHz | 1.246413508 s | 55 us | cycle start, 1.246418508 s | 10 cycles later, 1.246468508 s | 50.000 us | plan exactly 200000.000 Hz |
| Fine frequency, 187654.321 Hz | 1.483221236 s | 60 us | cycle start, 1.483226564 s | 10 cycles later, 1.483279854 s | 53.290 us | carrier periods mix 68 and 69 ticks to land between whole-tick frequencies |
| Fine frequency, 187655.615 Hz | 1.783424714 s | 60 us | cycle start, 1.783430042 s | 10 cycles later, 1.783483332 s | 53.290 us | 1.3 Hz above the last case. Over 10 cycles that's 0.4 ns, under the 2 ns sample step. The serial port shows the difference: `b7_f187655: plan M 179 L 122096 ticks, f 187655.615 Hz` |
| Build time, long buffer | 1.702 s | 100 ms | P1.11 rise, 1.703272726 s | P1.11 fall, 1.783401770 s | 80.13 ms | P1.11 is high while the 7631-word buffer for 187655 Hz is planned and compiled, then the 19 ms burst |

A cycle starts at an A pulse that directly follows a B pulse.

Measured over the whole capture (2 sequences): no sample with A and B both on (524654 rows). After correcting the analyzer's -8.29 ppm against the 200 kHz case, the frequency cases measure 187654.322, 187655.617 and 187664.045 Hz against plans of 187654.321, 187655.615 and 187664.042 Hz (line fit over ~3700 cycles each).
