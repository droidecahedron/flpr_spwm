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
The DK's serial port prints a line every second:

```
START sent 5, DONE received 5
```

The two numbers should match. This hasn't been run on a DK yet.
