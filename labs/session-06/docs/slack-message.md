**Session 6 - Model-based development** :white_check_mark:
Commit: <COMMIT_LINK>

**Path B**: YAML model + my own generator (`modelgen.py`) -> C99. The generator refuses a model that is incomplete (18 state x event pairs: 9 handled, 9 ignored, 0 unhandled), has non-exhaustive guards, or duplicate priorities. Generated code: 0 warnings with the strict GCC set, 0 clang-tidy findings (36 before, fixed in the generator, not the output).
The generated `mode_sm.c` replaces the hand-written `modes.c` in the Session 5 app: check_flight 11/11, fault campaign 6/6 with 0 false positives in 300 s, gs_modes 49/51 (D-07 below).

**State machine** (diagram attached, `docs/mode-state-machine.png`): NOMINAL / DEGRADED / SAFE, 12 transitions, one per 2 s supervisor cycle, unique priority per state; only ground commands leave SAFE/DEGRADED.

**Back-to-back** (hand `modes.c` vs generated, 10 726 cycles, 12 752 events):
- Single-event cycles: **0 / 7996 diverge** (502 in run 1: model v1 was missing `monitors_tighten` on SAFE entry -> fixed)
- Multi-event cycles: 998 / 2730 diverge, **0 unclassified**

**Divergence log** (`lab-6.3-equivalence/divergence-log.md`):
| ID | Divergence | Count | At fault |
|---|---|---|---|
| D-01 | SAFE entered with 1 Hz monitoring | 502 (run 1) | Model v1 - fixed |
| D-02 | Hand passes through an intermediate mode | 236 | Neither |
| D-03 | Ground cmd undoes a same-cycle safing | 105 | Hand-written (weakness) |
| D-04 | Fault lost to arrival order | 158 | Hand-written (defect) |
| D-05 | Two conflicting ground commands | 365 | Spec choice |
| D-06 | Same end state, different cause reported | 134 | Neither |
| D-07 | Mode change up to 2 s after the TC | 2/51 system checks | Cost of cyclic semantics |

**Coverage** `mode_sm.c`: 100 % statements, 94.5 % branches; 3 branches unreachable by construction (argued from the model).
**Traceability**: 13 requirements MM-010..MM-130, 0 gaps, down to generated line ranges and tests (15/15 pass).
**Position** (report section 6): MBD was worth it for the specification and verification (found D-01, D-03, D-04 that no Session 5 test could), not for code generation at this size.

Report: `labs/session-06/docs/mbd-report.md`. Runs are on the host simulation; SIS runs follow in a later commit.
