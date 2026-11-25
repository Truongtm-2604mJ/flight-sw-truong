# LOTUS-1 demonstration

Two ways to run the same 7-step scenario (`docs/mission.md`):

| | `demo/scenario.py` (`make scenario`) | `demo/run_demo.py` (`make demo`) |
|---|---|---|
| Path | ground codec straight to the spacecraft (secure link) | through the MCS: Yamcs → bridge / security unit → spacecraft |
| Use | CI on every push; evidence (`evidence/demo/scenario-*.log`, `.json`) | the screen recording |
| Needs | the host build, or SIS (`scripts/run-sis.sh demo`) | Java 17, `mcs/run-bridge.sh`, `mcs/run-yamcs.sh` |
| Time | about 2 min | about 3 min 45 s at `--pace 1` |

## Unattended: everything, video included

`nohup sh scripts/run-all.sh > run-all.log 2>&1 &` runs every host check, the SIS evidence (with
`RTEMS_PREFIX`) and `demo/record_demo.py`, which records the three Yamcs views headless and composes
`evidence/demo/lotus1-demo.mp4` with captions. It needs Java 17+, ffmpeg and Python Playwright. Results
go to `run-all-summary.txt`.

## Recording (4 minutes or less)

Setup, before pressing record:

```
mcs/run-bridge.sh fresh          # terminal 1 (add "sis" to run the RTEMS image on SIS)
mcs/run-yamcs.sh                 # terminal 2, then open http://localhost:8090 -> lotus1
```

In the browser, open three tabs: **Telemetry > Displays > operator.par**,
**Commanding > Command history**, and **Events**. Put a small terminal
(terminal 3) in a corner, where the captions will appear.

Dry run once (`python3 demo/run_demo.py --pace 0.3`), then restart the
bridge with `fresh` so the recording starts from boot. Record with:

```
python3 demo/run_demo.py --srt evidence/demo/captions.srt
```

| Time | Step | Show in Yamcs |
|---|---|---|
| 0:00-0:20 | 1. NOMINAL, HK to the MCS | operator.par: mode NOMINAL, uptime counting, T_BAT, V_BUS |
| 0:20-0:45 | 2. PAYLOAD ON, authenticated | Command history: the row with Accepted (TM(1,1)) and Complete (TM(1,7)); operator.par: payload_enabled / payload_on ON |
| 0:45-1:15 | 3. forged TC on the raw uplink | Command history: nothing new. operator.par: sec_rej_auth +1, payload still ON. Events: `SECURITY: TC rejected, class AUTH` |
| 1:15-1:45 | 4. T_BAT 48 °C | Events: one `T_BAT monitor ALARM`, `MODE NOMINAL -> DEGRADED`. operator.par: mode DEGRADED (yellow), payload_on OFF |
| 1:45-2:30 | 5. attempts 1, 2, 3, exhausted, SAFE | Events: three `THERMAL recovery attempt n/3`, `EXHAUSTED`, `MODE DEGRADED -> SAFE`. operator.par: SAFE (red), sched_enabled OFF. The refused PAYLOAD ON: Command history, TM(1,8) |
| 2:30-3:10 | 6. SAFE-01 | operator.par: cause MONITOR; TEST_INJECT_CLEAR, T_BAT back below 43 °C; SET_MODE NOMINAL (signed); mode NOMINAL. You can also run it from Stacks > SAFE-01. |
| 3:10-3:45 | 7. CI and coverage | Switch to the GitHub Actions run of the same commit (green), then `evidence/coverage/coverage.md` |

Narrate, or burn in the `.srt` captions (`ffmpeg -i demo.mp4 -vf subtitles=captions.srt out.mp4`).
Publish the video unlisted and put its link at the top of the README.
