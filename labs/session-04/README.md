# Session 4 - Onboard Data Handling and TM/TC Foundations

```
labs/session-04/
  lab-4.1-space-packet/    sp_encode / sp_decode and test suite
  lab-4.2-pus-service17/   CRC, PUS layer, TC handler, RTEMS TM/TC tasks,
                           ground station, exchange log
  lab-4.3-housekeeping/    periodic HK, limit monitor, events, service 3,
                           ground station, exchange log
  host-sim/                POSIX stand-in for the RTEMS Classic API (test aid)
  docs/tmtc-design.md      design note
  docs/sis-results.md      SIS results compared with my predictions
```

Later labs reuse earlier ones by path (4.2 compiles `lab-4.1/sp.c`, 4.3
compiles the 4.2 sources), so there is one copy of each module.

## What has been verified

| | Status |
|---|---|
| Unit tests on the host (`make test`): 132 + 542 + 229 checks, built with `-Werror` and Address/UB sanitizers | pass |
| Ground scenarios against the host simulation (`make demo`): 16 + 23 checks | pass |
| Cross build for RTEMS 6 / LEON3 (`make rtems`) | done |
| Lab 4.2 on SIS, automated scenario | 16 of 16 pass (`lab-4.2-pus-service17/logs/sis-exchange.log`) |
| Lab 4.3 on SIS, automated scenario | 23 of 23 pass (`lab-4.3-housekeeping/logs/sis-exchange.log`) |
| Timing run on SIS | 0 missed periods, U = 0.381 (`lab-4.3-housekeeping/logs/sis-timing.log`) |

The host simulation runs the same `lab4-2.c`, `lab4-3.c` and
`tmtc_tasks.c` as RTEMS, but every task is a plain pthread: it proves
the logic and the protocol and says nothing about timing.

## Host

```
make test     # all three unit test suites
make demo     # both ground scenarios, rewrites lab-4.x/logs/host-sim-exchange.log
```

## RTEMS on SIS

```
cd lab-4.2-pus-service17 && make rtems RTEMS_PREFIX=<rtems prefix>
python3 ground/gs_lab42.py --pty --spawn "sparc-rtems6-sis -leon3 -rt -r lab4-2.exe" --log logs/sis-exchange.log

cd ../lab-4.3-housekeeping && make rtems rtems-timing RTEMS_PREFIX=<rtems prefix>
python3 ground/gs_lab43.py --pty --spawn "sparc-rtems6-sis -leon3 -rt -r lab4-3.exe" --log logs/sis-exchange.log
sparc-rtems6-sis -leon3 -r lab4-3-timing.exe > logs/sis-timing.log
```

By hand: start `sparc-rtems6-sis -leon3 -rt -r lab4-2.exe`, paste
`TC:1810C00000062911010001306A`, press Enter; three `TM:` lines come back.

Three things about SIS that these commands depend on:

- **`-rt`.** Without it simulated time runs far ahead of wall time (the
  first hand-typed command was answered at onboard time 2179 s), and
  housekeeping floods the link. The timing run does not need it.
- **`--pty`.** SIS forwards stdin to the simulated UART only when stdin
  is a terminal. Through a pipe no telecommand arrives.
- **No `-nouartrx`.** Lab 3 used it; here it would disable the command
  link. Single CPU (no `-m`), as the timing analysis assumes.

The final `cpu 0 in error mode (tt = 0x80)` after `[ RTEMS shutdown ]`
is how RTEMS stops on SIS, not a fault.

## Notes for review

- **SIS, not QEMU**, as in Session 3.
- `sp_encode` derives the length field from `payload_len` and ignores
  `hdr->data_length`, so a header cannot disagree with its payload.
  `data_length` is filled by `sp_decode`.
- `labs/lab-3.2-periodic/lab3-2.c` as committed has `195000` iterations
  for ACS_CTRL, while `labs/docs/timing-analysis.md` gives the calibrated
  value `43000`. Lab 4.3 uses `43000`.
- The same workload loops run about 4 % faster here than in Lab 3.2
  (Makefile with `-O2` against `waf`), so the two labs' CPU times are not
  comparable loop for loop (design note, Section 5).
- Lab 4.3 calls `rtems_rate_monotonic_period()` at the top of each task
  loop instead of the bottom. With the Lab 3.2 order the first two jobs
  run back to back, which produced two housekeeping reports with the
  same timestamp at start up.
- Subtype numbers and secondary header layouts were cross-checked against
  open-source PUS-C implementations (Yamcs, spacepackets, FSFW), with no
  discrepancy found. Not yet checked against the ECSS-E-ST-70-41C text
  itself (design note, Section 6).
