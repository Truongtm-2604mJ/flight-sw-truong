# SIS results from the nightly-target CI job

The nightly-target job builds the RTEMS 6 toolchain (cached) and runs `make sis` on SIS. The results
below are copied from its log, for commit f4c05c4 (https://github.com/Truongtm-2604mJ/flight-sw-truong/actions/runs/). The job's own artifact holds the log files.
A local `make sis` writes the same files to `evidence/timing/`, `evidence/demo/`,
`evidence/test-runs/` and `evidence/validation/results-sis/`.

| Step | Result on SIS |
|---|---|
| Security operation cost (200 runs each) | accept max 1.326 ms, reject max 1.223 ms (TIM-020: 5 ms or less) |
| Integrated flight build, 60 s, rate monotonic statistics | 0 missed periods; every R <= T; U = 0.287 (see the note below) |
| Mission scenario, 7 steps | 28 passed, 0 failed |
| Security system tests (Session 8, migrated) | 57 passed, 1 failed. The failure is [8.1-6], key store persistence across reset, which fails by design on SIS (NCR-04) |
| Integration image (int_tc_path) | PASS |
| Validation campaign | 35 tests, 1 failed (VT-TMTC-040 = NCR-01, deferred) |

**Note on U = 0.287.** In this run PL_LOG measured 0.042 ms, because the integrated build boots with the
payload off and nothing enabled it during the 60 s run (PL-010). The figure is real but it is not the
worst case. Two things replace it:

- The worst case for PL_LOG is 95.67 ms, measured on SIS in the Session 8 build (same task code). With
  it, U = 0.383.
- From commit 'timing build: payload enabled', the timing image starts with the payload enabled
  (`-DTIMING_PAYLOAD_ON`, timing build only), so the next nightly run measures PL_LOG at its worst
  case directly.

Raw budget table from that run:

```
# LOTUS-1 timing budget (integrated application)

Operation costs: SIS (`sis-sec-timing.log`). Task set: SIS rate monotonic statistics of the integrated flight build, 60 s (`sis-rm.log`).

## Security operations

| Operation | estimate (ms) | SIS mean (ms) | SIS max (ms) | max vs estimate |
|---|---:|---:|---:|---:|
| plain | 0.553 | 0.276 | 0.293 | -47 % |
| accept | 1.996 | 1.310 | 1.326 | -34 % |
| reject | 1.823 | 1.206 | 1.223 | -33 % |
| scrub | 2.384 | 1.755 | 1.771 | -26 % |
| load | 2.160 | 1.577 | 1.591 | -26 % |

MAC cost per command (accept minus plain): 1.033 ms (estimate 1.443 ms).

## Task set

| Task | P | T (ms) | C (ms) | U | B (ms) | R (ms) | R <= T | missed | estimate C (ms) | C vs estimate |
|---|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|
| SUPERVISOR | 5 | 2000 | 0.064 | 0.0000 | 0.000 | 0.1 | yes | 0 | 5.000 | -99 % |
| ACS_CTRL | 10 | 100 | 18.958 | 0.1896 | 0.000 | 19.0 | yes | 0 | 18.957 | +0 % |
| TM_HK | 20 | 500 | 48.800 | 0.0976 | 2.917 | 70.7 | yes | 0 | 50.348 | -3 % |
| PL_LOG | 30 | 1000 | 0.042 | 0.0000 | 2.917 | 70.8 | yes | 0 | 95.644 | -100 % |
| SCHED | 45 | 1000 | 0.029 | 0.0000 | 2.917 | 70.8 | yes | 0 | 4.293 | -99 % |

Periodic utilisation U = 0.2873 (Session 4, three tasks: 0.3807; estimate for this task set: 0.3927). Liu and Layland bound for n = 5: 0.7435. Missed periods: 0.

TC_RX (priority 40, below the periodic tasks except SCHED) under a flood of invalid TCs, each costing one rejection:

| Uplink | TC/s max | TC_RX load | U with TC_RX |
|---|---:|---:|---:|
| lab console, 115200 baud, hex lines | 47.2 | 0.0577 | 0.3450 |
| S-band TC uplink, 4 kbit/s, 120 octet TCs | 3.9 | 0.0048 | 0.2921 |

Verdict: the task set CLOSES (every R <= T, no missed period).
```
