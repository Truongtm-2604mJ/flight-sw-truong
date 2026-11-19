# Timing budget with authentication

Operation costs: SIS (`sis-sec-timing.log`). Task set: SIS rate monotonic statistics of the Session 8 flight build, 60 s (`sis-rm.log`).

## Security operations

| Operation | estimate (ms) | SIS mean (ms) | SIS max (ms) | max vs estimate |
|---|---:|---:|---:|---:|
| plain | 0.553 | 0.276 | 0.288 | -48 % |
| accept | 1.996 | 1.310 | 1.326 | -34 % |
| reject | 1.823 | 1.206 | 1.223 | -33 % |
| scrub | 2.384 | 1.755 | 1.771 | -26 % |
| load | 2.160 | 1.577 | 1.593 | -26 % |

MAC cost per command (accept minus plain): 1.038 ms (estimate 1.443 ms).

## Task set

| Task | P | T (ms) | C (ms) | U | B (ms) | R (ms) | R <= T | missed | estimate C (ms) | C vs estimate |
|---|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|
| SUPERVISOR | 5 | 2000 | 0.063 | 0.0000 | 0.000 | 0.1 | yes | 0 | - | - |
| ACS_CTRL | 10 | 100 | 18.958 | 0.1896 | 0.000 | 19.0 | yes | 0 | 18.957 | +0 % |
| TM_HK | 20 | 500 | 48.797 | 0.0976 | 2.919 | 70.7 | yes | 0 | 50.348 | -3 % |
| PL_LOG | 30 | 1000 | 95.671 | 0.0957 | 2.919 | 185.4 | yes | 0 | 95.644 | +0 % |
| SCHED | 45 | 1000 | 0.029 | 0.0000 | 2.919 | 185.4 | yes | 0 | - | - |

Periodic utilisation U = 0.3829 (Session 4: 0.3807; Session 8 estimate for the three tasks: 0.3859). Liu and Layland bound for n = 5: 0.7435. Missed periods: 0.

TC_RX (priority 40, below the periodic tasks except SCHED) under a flood of invalid TCs, each costing one rejection:

| Uplink | TC/s max | TC_RX load | U with TC_RX |
|---|---:|---:|---:|
| lab console, 115200 baud, hex lines | 47.2 | 0.0577 | 0.4406 |
| S-band TC uplink, 4 kbit/s, 120 octet TCs | 3.9 | 0.0048 | 0.3877 |

Verdict: the task set CLOSES (every R <= T, no missed period).
