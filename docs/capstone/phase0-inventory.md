# Capstone Phase 0: inventory (Tran Manh Truong)

Target: **SIS** (LEON3, RTEMS 6). This project has used SIS since Session 3, with the same LEON3 BSP the
course uses under QEMU. Host simulation of the RTEMS API is used for fast tests.

| Component | From | Status | Works on target? | Evidence |
|---|---|---|---|---|
| RTOS periodic task framework | Lab 3.2 | complete | yes | `labs/session-03/docs/timing-analysis.md` (SIS run, rate monotonic statistics) |
| Priority inversion handling | Lab 3.3 | complete | yes | Lab 3.3 SIS runs. In the application: every shared lock is a binary semaphore with priority inheritance. |
| Space Packet encode and decode | Lab 4.1 | complete | yes | `labs/session-04/docs/sis-results.md` |
| PUS command path and verification | Lab 4.2 | complete | yes | TM(1,1/2/7/8), services 3, 5, 8, 11, 12, 17, 19 |
| Housekeeping and event reporting | Lab 4.3 | complete | yes | HK SID 1/2, event service 5 |
| Parameter monitoring | Lab 5.1 | complete | yes | V_BUS, T_BAT, T_OBC with persistence filtering |
| Liveness supervision | Lab 5.2 | complete | yes | heartbeat supervisor, bounded restarts, watchdog |
| Mode state machine | Lab 5.3 and 6.2 | complete (generated from the model) | yes | `model/mode_machine.yaml`, back-to-back equivalence with the hand-written one |
| Test suite and coverage | Labs 7.1, 7.2 | complete | integration level yes (SIS); unit tests on the host | Unity unit tests, integration tests, coverage with justifications, MC/DC, static analysis with dispositions |
| CI pipeline | Lab 7.3 | complete | nightly SIS job | fast checks on every push, nightly target, release evidence |
| Command authentication | Lab 8.1 (and 8.2 key management) | complete | yes on SIS for authentication. Key store persistence across reset: no (SIS has no NV memory, recorded as an open NCR) | HMAC-SHA-256/128, anti-replay counter, rekey, recovery key. SIS: 57/58 system tests, accept 1.33 ms max (commit f690ff8) |
| Mission control integration | Lab 8.3 | complete | yes (host simulation and SIS through the same bridge) | Yamcs 5.13.7, XTCE MDB generated and checked, SAFE-01 procedure |

**Proposed scope:** all three tiers (MUST, SHOULD, COULD). Every component exists. The capstone work
is integration into one application under `src/`, the mission scenario, and the evidence.

**Known gaps going in:**
- Two mode machines, two TM/TC task sets and two host simulations need merging.
- The Session 7 NCR-01 (TM(1,7) for a mode command is sent before the mode changes) is deferred.
- Session 8 is closed on SIS (commit f690ff8): U = 0.383, no missed period, TC authentication 1.33 ms max.
