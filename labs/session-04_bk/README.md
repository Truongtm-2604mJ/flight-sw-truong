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
  docs/sis-predictions.md  what the first SIS run should look like
```

Later labs reuse earlier ones by path (4.2 compiles `lab-4.1/sp.c`, 4.3
compiles the 4.2 sources), so there is one copy of each module.

## What has been verified, and what has not

| | Status |
|---|---|
| Unit tests on the host (`make test`): 132 + 542 + 229 checks, built with `-Werror` and Address/UB sanitizers | pass |
| Ground station scenarios against the application on the host simulation (`make demo`): 16 + 23 checks | pass |
| Cross build for RTEMS 6 / LEON3 (`make rtems`) | **not done** |
| Run on SIS, ground station connected to the simulated UART | **not done** |
| Timing measurement for Section 5 of the design note | **not done** - needs SIS |

The host simulation runs the same `lab4-2.c`, `lab4-3.c` and
`tmtc_tasks.c` that RTEMS will run, but every task is a plain pthread:
it proves the logic and the protocol, and says nothing about priorities,
preemption or timing.

## Host

```
make test     # all three unit test suites
make demo     # both ground scenarios, rewrites lab-4.x/logs/host-sim-exchange.log
```

## RTEMS on SIS (to do)

```
cd lab-4.2-pus-service17 && make rtems RTEMS_PREFIX=<your prefix>
cd lab-4.3-housekeeping  && make rtems rtems-timing RTEMS_PREFIX=<your prefix>
```

`rtems.mk` holds the compiler flags. They are the usual RTEMS 6 form but
untested; if Lab 3 was built with different flags, use those.

1. `sis -leon3 -r lab4-2.exe`, type nothing: the banner should appear.
2. Paste `TC:1810C00000062911010001306A` and press Enter. Three `TM:`
   lines should come back (acceptance, test report, completion). Compare
   with `docs/sis-predictions.md`.
3. Ground station: `python3 ground/gs_lab42.py --spawn "sis -leon3 -r ../lab4-2.exe" --log logs/sis-exchange.log`.
   This assumes SIS connects the console UART to its stdin/stdout when
   they are pipes. If it does not, attach the UART to a pty or TCP port
   with the SIS UART options and use `--tcp`, or use QEMU
   (`-serial tcp::5555,server`) with `--tcp localhost:5555`.
4. Same for Lab 4.3 with `gs_lab43.py`.
5. Run `lab4-3-timing.exe`, take the TM_HK maximum CPU time from the
   rate monotonic statistics and put it in Section 5 of the design note.

Things most likely to need attention on the target:

- **Console input.** `TC_RX` blocks in `read()` on the console. If
  nothing is received, check how the BSP console driver delivers input
  (polled or interrupt) and whether SIS forwards stdin.
- **Fatal error 38** (FPU use in a non-FP task, seen in Lab 3.3). The two
  I/O tasks already have `RTEMS_FLOATING_POINT`; the periodic tasks do
  not and do not call libc I/O.
- **Simulator speed.** The Lab 4.3 ground script judges everything on
  onboard timestamps. The Lab 4.2 script waits `--quiet` seconds of
  ground time for replies; raise it if replies arrive late.

## Notes for review

- **SIS, not QEMU**, as in Session 3.
- `sp_encode` derives the length field from `payload_len` and ignores
  `hdr->data_length`, so a header cannot disagree with its payload.
  `data_length` is filled by `sp_decode`.
- `labs/lab-3.2-periodic/lab3-2.c` as committed has `195000` iterations
  for ACS_CTRL, while `labs/docs/timing-analysis.md` gives the calibrated
  value `43000`. Lab 4.3 uses `43000`.
- Lab 4.3 calls `rtems_rate_monotonic_period()` at the top of each task
  loop instead of the bottom. With the Lab 3.2 order the first two jobs
  run back to back, which produced two housekeeping reports with the
  same timestamp at start up.
- Subtype numbers and secondary header layouts were cross-checked against
  open-source PUS-C implementations (Yamcs, spacepackets, FSFW), with no
  discrepancy found. Not yet checked against the ECSS-E-ST-70-41C text
  itself (design note, Section 6).
