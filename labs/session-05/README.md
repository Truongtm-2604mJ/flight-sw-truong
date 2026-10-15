# Session 5 - Fault Detection, Isolation and Recovery

```
labs/session-05/
  lab-5.1-monitoring/       monitor table, persistence, hysteresis, tests
  lab-5.2-liveness/         supervisor, bounded restart escalation, watchdog
                            model, NV reset-cause record, tests
  lab-5.3-safe-mode/        mode state machine, FDIR inhibit, onboard
                            schedule (suspended in SAFE), tests
  lab-5.4-fault-injection/  injection harness (test builds only), ground
                            scripts, logs, campaign results
  app/                      the RTEMS application that integrates 5.1 to 5.4
  host-sim/                 POSIX stand-in for the RTEMS API (test aid)
  docs/fdir-design.md       design note
```

The Session 4 Space Packet, CRC, PUS and telecommand handler sources
are reused by path from `../session-04`; nothing there was changed.

## Results

| | Result |
|---|---|
| Unit tests (`make test`): 217 + 10 775 + 100 checks, `-Werror`, Address/UB sanitizers | pass |
| Flight build has no injection code (`make check-flight`), and rejects TC(200,x) with code 7 | pass |
| Lab 5.3 scenario (`gs_modes.py`): 51 checks | pass |
| Lab 5.4 campaign: 6 injections + 300 s false positive run, 33 checks | pass |
| False positive run, 3600 s of onboard time: 0 detections, 43 of 43 glitches filtered | pass |

Measured on the host simulation (`host-sim/`), which runs the same `app/`
sources as RTEMS. Campaign table: `lab-5.4-fault-injection/results/`.

## Running

```
make test            # unit tests
make check-flight    # flight build contains no fault injection
make demo            # Lab 5.3 scenario + Lab 5.4 campaign, about 8 minutes
```

On RTEMS / SIS, from `app/` (same tools and flags as Session 4):

```
make rtems rtems-timing RTEMS_PREFIX=<rtems prefix>
python3 ../lab-5.4-fault-injection/ground/gs_modes.py --pty \
    --spawn "sparc-rtems6-sis -leon3 -rt -r lab5-test.exe"
python3 ../lab-5.4-fault-injection/ground/campaign.py --pty --nv-volatile \
    --spawn "sparc-rtems6-sis -leon3 -rt -r lab5-test.exe" --label sis
sparc-rtems6-sis -leon3 -r lab5-timing.exe
```

## Notes for review

- Heartbeats are per-task counters rather than flags cleared by the
  supervisor: one writer per word, so no lost heartbeat (design note,
  Section 4).
- The watchdog is a model (timer service routine in clock interrupt
  context) with an early warning stage that records the reset cause in
  NV before the reset.
- After a watchdog reset the vehicle boots into SAFE.
- The FDIR inhibit expires on its own (maximum 1 hour).
- Subtype numbers for services 8, 11, 12 and 19 follow open-source PUS-C
  implementations and have not been checked against the ECSS text.
