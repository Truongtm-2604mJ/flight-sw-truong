# Validation test specification

Generated from `tests/validation/run_campaign.py`: the specification below is the same data the campaign executes. Each run starts a fresh spacecraft with blank NV memory unless stated; tests in a run execute in the order listed and some hand state to the next, as their preconditions say.

Verdicts: PASS, FAIL, N/A (cannot be shown on this target, with the reason), OBSERVED (exploratory test, no requirement, result recorded for validation).

## R1 - TM/TC basics

### VT-01: Cold boot comes up in NOMINAL with no transition report

- **Requirements:** MM-090
- **Preconditions:** Blank NV memory.
- **Steps:**
  1. Start the spacecraft.
  2. Read the boot report TM(5,1) and the first 3 s of telemetry.
- **Expected:** Boot report: reset cause none, NV blank, mode NOMINAL. HK mode NOMINAL. No mode transition event.

### VT-TMTC-010a: Acknowledged TC(17,1) in NOMINAL; reports follow the flags

- **Requirements:** TMTC-010
- **Preconditions:** Spacecraft in NOMINAL.
- **Steps:**
  1. Send TC(17,1) with acknowledgement flags 1001 (acceptance, completion).
  2. Send TC(17,1) with flags 0000.
- **Expected:** First: TM(1,1), TM(17,2), TM(1,7) in that order, each verification report carrying the TC's request ID and addressed to its source ID. Second: TM(17,2) only.

### VT-TMTC-020: Acceptance failures: reported with the first failed check's code, never executed

- **Requirements:** TMTC-020, MM-120
- **Preconditions:** Spacecraft in NOMINAL. Read the HK TC rejected counter.
- **Steps:**
  1. Send TC(17,1) with a corrupted CRC.
  2. Send TC(17,1) to APID 0x055.
  3. Send TC(99,1).
  4. Send TC(17,9).
  5. Send TC(8,1) with mode value 3.
  6. Send TC(19,9) with a duration of 0 s.
  7. Wait 3 s; read HK.
- **Expected:** TM(1,2) codes 3, 5, 7, 8, 9, 9 in that order; no TM(1,7) and no TM(17,2) for any of them; mode still NOMINAL; HK TC rejected counter up by 6.

### VT-TMTC-030: Undecodable lines are discarded and counted

- **Requirements:** TMTC-030
- **Preconditions:** Spacecraft in NOMINAL. Read the HK TC rejected counter.
- **Steps:**
  1. Send the lines `TC:181` (odd digits), `TC:ZZ10` (not hex), a line of 121 octets, and `TC:1810` (shorter than a primary header).
  2. Wait 3 s; read HK; send TC(17,1).
- **Expected:** No TM(1,x) for any of the four lines; HK TC rejected counter up by 4; TC(17,1) still answered.

### VT-TMTC-060: Full housekeeping every 1.0 s in NOMINAL

- **Requirements:** TMTC-060
- **Preconditions:** Spacecraft in NOMINAL.
- **Steps:**
  1. Record HK SID 1 reports for 12 s.
  2. Compute the onboard time between consecutive reports.
- **Expected:** At least 11 intervals, every one within 0.9 s to 1.1 s.

## R2 - ground mode commands, SAFE

### VT-TMTC-040: Ground SAFE from NOMINAL: completion report, effect, report and latency

- **Requirements:** TMTC-040
- **Preconditions:** Fresh spacecraft in NOMINAL. A TC(17,1) with source ID 0x0042 scheduled 6 s ahead (prepared for VT-MM-020).
- **Steps:**
  1. Send TC(8,1) SAFE.
  2. Note the onboard time of its TM(1,7) and of the mode transition report TM(5,x).
  3. Read the first HK report generated after TM(1,7).
- **Expected:** The mode transition is reported no later than TM(1,7), and the first HK after TM(1,7) shows SAFE. (The same execution provides the data for VT-MM-150 and VT-MM-080.)

### VT-MM-150: Ground mode command latency

- **Requirements:** MM-150
- **Preconditions:** As VT-TMTC-040 (same execution).
- **Steps:**
  1. Use the times recorded in VT-TMTC-040.
- **Expected:** Transition reported at most 3.0 s after TM(1,7).

### VT-MM-080: Transition report contents

- **Requirements:** MM-080, MM-070
- **Preconditions:** As VT-TMTC-040 (same execution).
- **Steps:**
  1. Decode the transition report recorded in VT-TMTC-040.
- **Expected:** From NOMINAL to SAFE, cause GROUND.

### VT-MM-030: SAFE configuration as seen from the ground

- **Requirements:** MM-030
- **Preconditions:** Vehicle in SAFE (VT-TMTC-040).
- **Steps:**
  1. Record telemetry for 6 s after the transition report.
- **Expected:** Only essential HK (SID 2) after the transition; status: payload off, schedule disabled, sun pointing, receiver on; mode SAFE.

### VT-TMTC-010b: Commands are answered in SAFE

- **Requirements:** TMTC-010
- **Preconditions:** Vehicle in SAFE.
- **Steps:**
  1. Send TC(17,1) with flags 1001.
- **Expected:** TM(1,1), TM(17,2), TM(1,7).

### VT-MM-020a: Scheduled command does not fire in SAFE

- **Requirements:** MM-020
- **Preconditions:** Vehicle in SAFE; TC(17,1) tagged 0x0042 scheduled before SAFE was entered.
- **Steps:**
  1. Wait until 3 s after the scheduled release time.
- **Expected:** No TM(17,2) addressed to 0x0042.

### VT-MM-010: Autonomous events never leave SAFE

- **Requirements:** MM-010, MM-050
- **Preconditions:** Vehicle in SAFE.
- **Steps:**
  1. Inject T_BAT 48 C (sustained) for 6 s, then V_BUS 25.0 V (sustained) for 6 s, then clear.
  2. Observe 6 s more.
- **Expected:** Alarms are reported, but no mode transition; mode SAFE throughout.

### VT-MM-110: Two conflicting ground commands in one cycle

- **Requirements:** MM-110, MM-140
- **Preconditions:** Vehicle in SAFE.
- **Steps:**
  1. Send TC(8,1) DEGRADED and TC(8,1) NOMINAL back to back (a few ms apart, so in the same 2 s cycle).
  2. Observe 4 s.
- **Expected:** Exactly one transition, SAFE to DEGRADED (priority 1 in SAFE); a discard report (event 0x0404) naming tc_mode_nominal; mode DEGRADED. If the two commands fall in different cycles (seen as two transitions), the vehicle is returned to SAFE and the step repeated, at most 3 times.

### VT-MM-020b: Leaving SAFE does not resume the schedule; the ground does

- **Requirements:** MM-020
- **Preconditions:** Vehicle in DEGRADED after SAFE; scheduled command overdue.
- **Steps:**
  1. Wait 3 s.
  2. Send TC(11,1) enable the schedule.
  3. Wait 3 s.
- **Expected:** No TM(17,2) to 0x0042 before TC(11,1); exactly one after it.

## R3 - monitoring and inhibit

### VT-FDIR-010: Single sample glitch is filtered and counted

- **Requirements:** FDIR-010
- **Preconditions:** Fresh spacecraft in NOMINAL.
- **Steps:**
  1. Read HK filtered counter.
  2. Inject T_BAT 48 C for one sample.
  3. Observe 6 s.
- **Expected:** No monitor event, no transition; mode NOMINAL; filtered counter up by at least 1.

### VT-FDIR-020: Sustained T_BAT violation: one alarm, DEGRADED

- **Requirements:** FDIR-020, MM-040
- **Preconditions:** Vehicle in NOMINAL (1 Hz monitoring).
- **Steps:**
  1. Inject T_BAT 48 C (sustained).
  2. Observe 12 s.
- **Expected:** One T_BAT alarm event no later than 4 s after TM(1,7) of the injection (persistence 3 + 1 sample periods); no second T_BAT alarm; transition NOMINAL to DEGRADED, cause MONITOR.

### VT-MM-050: No autonomous recovery when the parameter returns

- **Requirements:** MM-050
- **Preconditions:** Vehicle in DEGRADED with T_BAT in alarm (VT-FDIR-020).
- **Steps:**
  1. Clear the injection.
  2. Wait for the T_BAT cleared event; observe 10 s more.
- **Expected:** T_BAT cleared event; no transition; mode DEGRADED.

### VT-OBS-01: Exploratory: ground returns to NOMINAL while an alarm is still active

- **Requirements:** none (exploratory)
- **Preconditions:** Vehicle in DEGRADED. Validation question, no requirement: what does the operator get?
- **Steps:**
  1. Inject T_BAT 48 C (sustained); wait for the alarm event, then 3 s more (a later cycle).
  2. Send TC(8,1) NOMINAL.
  3. Observe 8 s.
  4. Clear the injection.
- **Expected:** Recorded, not judged: the resulting mode, the T_BAT monitor state in HK, and whether any event tells the operator that the alarm is still active.

### VT-FDIR-090: Per-parameter monitoring disable and enable

- **Requirements:** FDIR-090
- **Preconditions:** Vehicle in NOMINAL.
- **Steps:**
  1. TC(12,2) T_BAT.
  2. Inject T_BAT 48 C (sustained); observe 6 s.
  3. TC(12,1) T_BAT; observe 6 s.
  4. Clear; ground NOMINAL.
- **Expected:** No T_BAT event while disabled; one T_BAT alarm within 6 s of re-enabling.

### VT-MM-060: Inhibit: autonomous change refused and reported; ground still obeyed

- **Requirements:** MM-060, MM-070
- **Preconditions:** Vehicle in NOMINAL.
- **Steps:**
  1. TC(19,9) inhibit 120 s.
  2. Inject T_BAT 48 C (sustained); observe 8 s.
  3. TC(8,1) DEGRADED; observe 4 s.
  4. TC(19,8); clear; ground NOMINAL.
- **Expected:** Alarm reported; ACTION INHIBITED event (kind mode change); mode stays NOMINAL. The ground command is applied: NOMINAL to DEGRADED, cause GROUND.

### VT-FDIR-080: Inhibit expires by itself and says so

- **Requirements:** FDIR-080
- **Preconditions:** Vehicle in NOMINAL, FDIR enabled.
- **Steps:**
  1. TC(19,9) inhibit 5 s.
  2. Wait up to 10 s for the inhibit OFF event.
- **Expected:** Inhibit ON event; OFF event with reason timeout 4.5 s to 7 s after TM(1,7); HK inhibit bit cleared.

## R4 - under voltage

### VT-FDIR-030: Bus under voltage puts the vehicle in SAFE

- **Requirements:** FDIR-030, MM-040
- **Preconditions:** Fresh spacecraft in NOMINAL.
- **Steps:**
  1. Inject V_BUS 25.0 V (sustained).
  2. Observe 8 s.
- **Expected:** V_BUS alarm (low); transition NOMINAL to SAFE with cause MONITOR, within 6 s of TM(1,7) (detection 4 s + one supervisor period).

## R5 - single task hang

### VT-FDIR-040: One heartbeat miss: restart, no mode change

- **Requirements:** FDIR-040
- **Preconditions:** Fresh spacecraft in NOMINAL.
- **Steps:**
  1. Inject a PL_LOG hang (once).
  2. Observe 8 s.
- **Expected:** HEARTBEAT MISSED and TASK RESTARTED events for PL_LOG; no transition; mode NOMINAL. Timing: the hang begins at PL_LOG's next release, which the ground cannot see, up to one PL_LOG period (1.0 s) after TM(1,7); so the restart must be reported no later than 5.0 s after TM(1,7) (4.0 s + 1.0 s).

## R6 - repeated task failure

### VT-FDIR-050: Repeated failure: bounded restarts, escalation to SAFE

- **Requirements:** FDIR-050, MM-040
- **Preconditions:** Fresh spacecraft in NOMINAL.
- **Steps:**
  1. Inject an ACS_CTRL hang five times.
  2. Observe until the escalation event, then 6 s more.
- **Expected:** Three TASK RESTARTED events, then TASK ESCALATED; transition to SAFE with cause LIVENESS; no restart after the escalation.

## R6b - repeated payload task failure

### VT-FDIR-050b: Repeated PL_LOG failure: escalation to DEGRADED, failed task flagged in HK

- **Requirements:** FDIR-050
- **Preconditions:** Fresh spacecraft in NOMINAL. Added after the coverage analysis (coverage.md, C-04).
- **Steps:**
  1. Inject a PL_LOG hang five times.
  2. Observe until the escalation event, then 6 s more.
- **Expected:** Three TASK RESTARTED events, then TASK ESCALATED for PL_LOG; transition to DEGRADED with cause LIVENESS; no restart after the escalation; full HK shows PL_LOG (bit 2) in the failed mask.

## R7 - watchdog

### VT-FDIR-060: Supervisor hang: watchdog warning, reset, boot in SAFE

- **Requirements:** FDIR-060, MM-090
- **Preconditions:** Fresh spacecraft in NOMINAL.
- **Steps:**
  1. Inject a supervisor hang.
  2. Wait for the watchdog warning and the reset (process exit).
  3. Restart the spacecraft keeping the NV memory; read the boot report and HK.
- **Expected:** Warning no later than 6 s after TM(1,7); reset follows; boot report: last reset WATCHDOG, mode SAFE; HK cause WATCHDOG_RESET; no transition report at boot. On a target without persistent NV (SIS) the last part cannot be shown: N/A.

## R8 - false positive run

### VT-FDIR-070: No false alarm in nominal operation

- **Requirements:** FDIR-070
- **Preconditions:** Fresh spacecraft in NOMINAL, no injection.
- **Steps:**
  1. Observe for the false positive duration (300 s of onboard time by default).
- **Expected:** No monitor alarm, heartbeat event, restart or transition; mode NOMINAL.

## After all test build runs

### VT-TMTC-050: Telemetry integrity over every run of the campaign

- **Requirements:** TMTC-050
- **Preconditions:** All runs R1 to R8 completed.
- **Steps:**
  1. For each boot: check per-APID sequence counts.
  2. Count the lines the ground rejected.
  3. Read the TM drop counter in the last full HK of each boot.
- **Expected:** Every per-APID sequence advances by exactly 1; 0 lines rejected by the ground; TM drop 0.

## R9 - flight build

### VT-TMTC-070: Flight build has no fault injection

- **Requirements:** TMTC-070
- **Preconditions:** Fresh flight build in NOMINAL.
- **Steps:**
  1. Send TC(200,1) T_BAT 48 C, TC(200,2), TC(200,3), TC(200,4), TC(200,99).
  2. Observe 5 s.
- **Expected:** Five TM(1,2) with code 7; no TM(1,7); no event; mode NOMINAL; T_BAT unchanged.

## Analysis and integration level evidence

### VT-MM-100: Completeness of the mode model (analysis)

- **Requirements:** MM-100
- **Preconditions:** Model vehicle_mode v3.
- **Steps:**
  1. Read the generator's completeness report and its negative test results.
- **Expected:** 0 unhandled (state, event) pairs; generator negative tests all pass.

### VT-MM-130: Fault in the same cycle as a ground recovery (integration level)

- **Requirements:** MM-130
- **Preconditions:** Not reproducible through the ground interface: the ground cannot choose the evaluation cycle a command lands in.
- **Steps:**
  1. Read the integration suite result IT-10 (tests/integration, same flight code).
- **Expected:** IT-10 PASS.
