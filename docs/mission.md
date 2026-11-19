# LOTUS-1 reference mission

**LOTUS-1** is a 12U Earth observation demonstration satellite in a 520 km
sun-synchronous low Earth orbit. It images coastal areas and river deltas on
ground request, and it is operated from one ground station.

## Mission statement

> LOTUS-1 shall take images of a commanded target area during a planned
> pass and downlink them at the next ground contact. It shall stay safe and
> commandable between contacts without any ground action. It shall accept
> commands only from the LOTUS-1 control centre.

## What the flight software has to deal with

| Item | Value | Consequence for the software |
|---|---|---|
| Orbit | 520 km SSO, period about 95 min | 4 to 6 contacts a day |
| Ground contact | about 8 min per pass, 4 kbit/s S-band TC uplink, HK at 1 Hz on the downlink | The vehicle spends more than 90 % of its time alone. The FDIR acts autonomously and the schedule (PUS 11) carries the planned activities. |
| Power | 28 V nominal bus, simulated (`V_BUS`); under voltage below 26.0 V | A critical fault, so SAFE (FDIR-030) |
| Thermal | battery temperature `T_BAT` (alarm above 45.00 °C, warning above 43.00 °C), OBC temperature `T_OBC` | Non-critical: DEGRADED, then bounded recovery attempts, then SAFE (FDIR-100, FDIR-110) |
| Payload | imager, logging task PL_LOG at 1 Hz | Runs only on ground request and only in NOMINAL. Suspended in DEGRADED, off in SAFE (PL-010, PL-020). |
| OBC | LEON3 under RTEMS 6, simulated by SIS. Host simulation for development. | One application with 7 tasks and a 2 s supervisor cycle (`docs/architecture.md`) |
| Uplink security | the uplink frequency can be reached by anyone with a transmitter | HMAC-SHA-256/128 on every TC, replay counter, key management (SEC-010 to SEC-040) |

## Modes

| Mode | Entered | Payload | Telemetry | Schedule | Exit |
|---|---|---|---|---|---|
| NOMINAL | cold boot, ground command | on if the ground enabled it | full HK SID 1, 1 Hz | running | ground command, or FDIR |
| DEGRADED | non-critical fault (thermal alarm, liveness), ground command | suspended (the ground enable is kept) | full HK | running | ground command only |
| SAFE | critical fault, recovery exhausted, watchdog reset, ground command | off, ground enable cleared | essential HK SID 2 | suspended | authenticated ground command only (procedure SAFE-01) |

## The demonstration scenario

The scenario below is run in the recording (through the MCS, `demo/run_demo.py`).
It is also run automatically in CI (`make scenario`, `demo/scenario.py`).

1. Boot to NOMINAL. HK reaches the MCS.
2. Authenticated TC enables the payload. The MCS shows the acceptance report
   TM(1,1) and the completion report TM(1,7).
3. A TC with an invalid authentication tag arrives on the raw uplink. It is
   rejected and a security event is raised. The payload is unchanged.
4. Thermal anomaly (T_BAT forced to 48 °C). The persistence filter needs 3
   samples. One event is raised, the mode goes to DEGRADED and the payload is
   suspended.
5. Three recovery attempts run 10 s apart and fail. The recovery is
   exhausted and the vehicle goes to SAFE: payload off, schedule suspended,
   essential TM only.
6. Procedure SAFE-01 (Lab 8.3): assess the cause, clear the fault, exit SAFE
   by authenticated command, confirm NOMINAL.
7. CI is green and the coverage report is available for the same commit.
