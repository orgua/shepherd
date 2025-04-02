# SYNC_PROCESS between kernel-module & PRU

## Main Goal

- synchronize system time with pru
- adapting period length of loop (like PLL) via clock-skewing
- see python-simulation for details regarding PLL & PI-Tuning

## Mechanism High Level Init

- PRU1 signals ready-to-start with sync-reset-message
- kMod reacts by sending a TS for the next interrupt
- PRU1 sets TS on interrupt and starts main-loop

## Mechanism High level Runtime

- Kernel sends pseudo-interrupt to PRU1 for taking a counter-snapshot
  - both, kMod and PRU, take a timestamp
- PRU sends current period-counter1 and switches to "reply pending"
- Kernel feeds the TS-difference into a PI-Controller and calculates a value for clock-correction
- PRU waits for response message to adjust clock-skew
