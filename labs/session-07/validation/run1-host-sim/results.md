# Validation campaign results: host-sim

- Executed: 2026-10-26 10:03 UTC, 8.1 min
- Spacecraft: `lab6-test.host` (test build), `lab6-flight.host` (flight build)
- Log: `campaign.log` (every TC and TM, decoded)
- Verdicts: FAIL 3, OBSERVED 1, PASS 26

| Test | Requirements | Expected | Actual | Verdict |
|---|---|---|---|---|
| VT-01 | MM-090 | Boot report: reset cause none, NV blank, mode NOMINAL. HK mode NOMINAL. No mode transition event. | boot report mode NOMINAL, last reset 0, NV valid 0; HK mode NOMINAL; 0 transition events | **PASS** |
| VT-TMTC-010a | TMTC-010 | First: TM(1,1), TM(17,2), TM(1,7) in that order, each verification report carrying the TC's request ID and addressed to its source ID. Second: TM(17,2) only. | flags 1001: [(1, 1), (17, 2), (1, 7)], request ID echoed True; flags 0000: [(17, 2)] | **PASS** |
| VT-TMTC-020 | TMTC-020, MM-120 | TM(1,2) codes 3, 5, 7, 8, 9, 9 in that order; no TM(1,7) and no TM(17,2) for any of them; mode still NOMINAL; HK TC rejected counter up by 6. | codes [3, 5, 7, 8, 9, 9]; 0 executions; mode NOMINAL; TC rejected counter +6 | **PASS** |
| VT-TMTC-030 | TMTC-030 | No TM(1,x) for any of the four lines; HK TC rejected counter up by 4; TC(17,1) still answered. | 0 verification reports; TC rejected counter +3; alive True | **FAIL** |
| VT-TMTC-060 | TMTC-060 | At least 11 intervals, every one within 0.9 s to 1.1 s. | 11 intervals, min 0.998 s, max 1.002 s | **PASS** |
| VT-TMTC-040 | TMTC-040, MM-070, MM-080, MM-150 | TMTC-040: the mode transition is reported no later than TM(1,7), and the first HK after TM(1,7) shows SAFE. MM-070/MM-080: transition NOMINAL to SAFE reported with cause GROUND. MM-150: at most 3.0 s from TM(1,7) to the transition report. | TM(1,7) at 0.004 s, transition report at 2.003 s (+1.999 s after TM(1,7)); first HK after TM(1,7): NOMINAL | **FAIL** |
| VT-MM-150 | MM-150 | Transition reported at most 3.0 s after TM(1,7). | 1.999 s | **PASS** |
| VT-MM-080 | MM-080, MM-070 | From NOMINAL to SAFE, cause GROUND. | NOMINAL -> SAFE, cause GROUND | **PASS** |
| VT-MM-030 | MM-030 | Only essential HK (SID 2) after the transition; status: payload off, schedule disabled, sun pointing, receiver on; mode SAFE. | HK SIDs after the transition [1, 2]; status 0x18 (payload False, schedule False, sun True, RX True); mode SAFE | **FAIL** |
| VT-TMTC-010b | TMTC-010 | TM(1,1), TM(17,2), TM(1,7). | [(1, 1), (17, 2), (1, 7)] | **PASS** |
| VT-MM-020a | MM-020 | No TM(17,2) addressed to 0x0042. | scheduled command fired: False | **PASS** |
| VT-MM-010 | MM-010, MM-050 | Alarms are reported, but no mode transition; mode SAFE throughout. | 4 monitor events, 0 transitions, mode SAFE | **PASS** |
| VT-MM-110 | MM-110, MM-140 | Exactly one transition, SAFE to DEGRADED (priority 1 in SAFE); a discard report (event 0x0404) naming tc_mode_nominal; mode DEGRADED. If the two commands fall in different cycles (seen as two transitions), the vehicle is returned to SAFE and the step repeated, at most 3 times. | attempt 1: 1 transition SAFE -> DEGRADED, 1 discard report(s) naming event [0], mode DEGRADED | **PASS** |
| VT-MM-020b | MM-020 | No TM(17,2) to 0x0042 before TC(11,1); exactly one after it. | before enable: 0; after enable: 1 | **PASS** |
| VT-FDIR-010 | FDIR-010 | No monitor event, no transition; mode NOMINAL; filtered counter up by at least 1. | 0 events; mode NOMINAL; filtered +1 | **PASS** |
| VT-FDIR-020 | FDIR-020, MM-040 | One T_BAT alarm event no later than 4 s after TM(1,7) of the injection (persistence 3 + 1 sample periods); no second T_BAT alarm; transition NOMINAL to DEGRADED, cause MONITOR. | 1 alarm event(s), first 2.997 s after TM(1,7); transitions [('NOMINAL', 'DEGRADED', 'MONITOR')] | **PASS** |
| VT-MM-050 | MM-050 | T_BAT cleared event; no transition; mode DEGRADED. | cleared event True; 0 transitions; mode DEGRADED | **PASS** |
| VT-OBS-01 | - | Recorded, not judged: the resulting mode, the T_BAT monitor state in HK, and whether any event tells the operator that the alarm is still active. | mode NOMINAL; T_BAT monitor ALARM; events after the command: [('0x401', None, 0)] | **OBSERVED** |
| VT-FDIR-090 | FDIR-090 | No T_BAT event while disabled; one T_BAT alarm within 6 s of re-enabling. | T_BAT events while disabled: 0; alarms after enable: 1 | **PASS** |
| VT-MM-060 | MM-060, MM-070 | Alarm reported; ACTION INHIBITED event (kind mode change); mode stays NOMINAL. The ground command is applied: NOMINAL to DEGRADED, cause GROUND. | alarm True; 1 refusal(s); mode after fault NOMINAL; ground DEGRADED applied True | **PASS** |
| VT-FDIR-080 | FDIR-080 | Inhibit ON event; OFF event with reason timeout 4.5 s to 7 s after TM(1,7); HK inhibit bit cleared. | OFF after 5.499 s, reason 2; HK inhibited bit False | **PASS** |
| VT-FDIR-030 | FDIR-030, MM-040 | V_BUS alarm (low); transition NOMINAL to SAFE with cause MONITOR, within 6 s of TM(1,7) (detection 4 s + one supervisor period). | V_BUS alarm True; SAFE after 3.998 s, cause MONITOR | **PASS** |
| VT-FDIR-040 | FDIR-040 | HEARTBEAT MISSED and TASK RESTARTED events for PL_LOG; no transition; mode NOMINAL. Timing: the hang begins at PL_LOG's next release, which the ground cannot see, up to one PL_LOG period (1.0 s) after TM(1,7); so the restart must be reported no later than 5.0 s after TM(1,7) (4.0 s + 1.0 s). | missed 1, restarted 1 after 4.008 s; 0 transitions | **PASS** |
| VT-FDIR-050 | FDIR-050, MM-040 | Three TASK RESTARTED events, then TASK ESCALATED; transition to SAFE with cause LIVENESS; no restart after the escalation. | 3 restarts, escalation True, 0 restarts after it; transitions [('SAFE', 'LIVENESS')] | **PASS** |
| VT-FDIR-060 | FDIR-060, MM-090 | Warning no later than 6 s after TM(1,7); reset follows; boot report: last reset WATCHDOG, mode SAFE; HK cause WATCHDOG_RESET; no transition report at boot. On a target without persistent NV (SIS) the last part cannot be shown: N/A. | warning after 5.499 s, reset True; boot report last reset 1, mode SAFE; HK cause WATCHDOG_RESET; 0 transition reports at boot | **PASS** |
| VT-FDIR-070 | FDIR-070 | No monitor alarm, heartbeat event, restart or transition; mode NOMINAL. | 300 s observed; unexpected events []; mode NOMINAL | **PASS** |
| VT-TMTC-050 | TMTC-050 | Every per-APID sequence advances by exactly 1; 0 lines rejected by the ground; TM drop 0. | 646 packets in 9 boots; no gap, no rejection, no drop | **PASS** |
| VT-TMTC-070 | TMTC-070 | Five TM(1,2) with code 7; no TM(1,7); no event; mode NOMINAL; T_BAT unchanged. | codes [7, 7, 7, 7, 7]; events []; mode NOMINAL, T_BAT 25.02 C | **PASS** |
| VT-MM-100 | MM-100 | 0 unhandled (state, event) pairs; generator negative tests all pass. | 0 unhandled pairs (completeness.md); generator tests pass True | **PASS** |
| VT-MM-130 | MM-130 | IT-10 PASS. | IT-10 PASS (int_tc_path.host.log) | **PASS** |
