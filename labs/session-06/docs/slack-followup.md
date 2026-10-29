**Session 6 - follow-up on the two open points**
Commit: <COMMIT_LINK>

**1. RTEMS/LEON3 evidence (SIS)**: `labs/session-06/app/run-sis.sh` builds `lab6-test.exe` / `lab6-flight.exe`, runs gs_modes, check_flight and the fault campaign under SIS, and measures the mode command latency. Results: `lab-6.2-generation/logs/sis-summary.md` (+ `sis-*.log`, `campaign-sis-generated.md`, `latency-sis.md`).
<SIS_RESULTS>

**2. D-05 closed in the model, not left implicit**
- Model **v3** + generator 1.1: a ground command that is not applied is now **always reported** (event 0x0404), also when the start state "ignores" it but another event moves the vehicle in the same cycle. New semantics rule `ignored_ground_command: report_if_state_changed`.
- New requirement **MM-140**; test **TC-GND-02** checks every start state x every set of ground commands, alone or with a fault, inhibited or not. Run against the v2 code it fails on **57** combinations; v3: 0.
- Back-to-back rerun (run 3): counts unchanged, single-event cycles still 0 / 7996 diverge.

**D-07** is now requirement **MM-150**: a ground mode command takes effect and is reported ≤ **3.0 s** after its TM(1,7). Bound: 2 s supervisor period + 10 ms tick jitter (supervisor is the highest-priority supervised task) -> ~1 s margin; justification in `semantics.md` ("Latency requirement": ground mode commands are not a fast loop, the time-critical path is the autonomous one). Measured max **1.999 s** (host), unit test TC-LAT-01. The same code also passed the Session 5 scenarios again: check_flight 11/11, campaign 6/6 + 0 false positives in 300 s, gs_modes 49/51 (the 2 = D-07).

Traceability: 15 requirements, 0 gaps; requirement tests 17/17.
