# MC/DC: hand-built test cases for the FDIR guard decisions

The lab asks for at least two compound decisions; this covers five, all FDIR guards. For each one the table gives:

- the conditions;
- one test case per row, named as in `tests/unit/`;
- the outcome;
- the **independence pairs**: two cases that differ only in one condition and give different outcomes.

C has short-circuit evaluation, so a condition that is not evaluated is shown as "–". This is the masking form of MC/DC: a pair must still differ in exactly the evaluated condition under test.

The evidence comes from two independent sources:

1. **Tool measurement.** clang 18 source-based coverage with `-fcoverage-mcdc` measures every decision of every flight module at unit level (`mcdc-clang-summary.txt`, `mcdc-clang-detail.txt`). It confirms the pairs below.
2. **Mutation check.** `mcdc-mutation.md` replaces each condition with the constant 1, one at a time, and runs the module's suite. If a condition's mutation is detected, the condition was shown to matter on its own.

Both are produced by `make -C tests mcdc`.

## MC-1: monitor clear band (hysteresis), `monitor.c` line 55

`if (value >= e->limit_low_clear && value <= e->limit_high_clear)`, evaluated while the monitor is in ALARM. The outcome is whether the sample counts towards clearing (true) or restarts the clear count (false). Test limits: low clear -90, high clear 90.

| Case | Test | A: value >= low clear | B: value <= high clear | Outcome |
|---|---|---|---|---|
| 1 | `test_mcdc_MC1_case1_A1_B1_in_band_counts` (value 0) | T | T | T |
| 2 | `test_mcdc_MC1_case2_A0_B1_below_low_clear_restarts` (value -95) | F | – | F |
| 3 | `test_mcdc_MC1_case3_A1_B0_above_high_clear_restarts` (value 95) | T | F | F |

Independence: **A by (1, 2)**, **B by (1, 3)**. Clang measured 100 %. Mutation: A killed by case 2; B killed by case 3 (and 2 older tests).

## MC-2: monitor jumps from one limit to the other, `monitor.c` line 89

`if (side != e->side && e->violation_count > 0u)`, evaluated on a violating sample in OK or WARN. The outcome is whether the run on the old side is abandoned and counted as a filtered transient (true).

| Case | Test | A: side changed | B: run in progress | Outcome |
|---|---|---|---|---|
| 1 | `test_mcdc_MC2_case1_A1_B1_flip_counts_abandoned_run` (+150 then -150) | T | T | T |
| 2 | `test_mcdc_MC2_case2_A0_B1_same_side_continues_run` (+150, +150) | F | – | F |
| 3 | `test_mcdc_MC2_case3_A1_B0_first_violation_starts_run` (0 then -150) | T | F | F |

Independence: **A by (1, 2)**, **B by (1, 3)**. Clang measured 100 %. Both mutations killed: A by 13 tests (MC-2 case 2 among them), B by the three MC-2 cases and 4 older tests.

## MC-3: supervisor, are FDIR actions inhibited? `supervisor.c` line 88

`inhibited = (s->ops->actions_inhibited != NULL && s->ops->actions_inhibited(s->arg))`, evaluated when a task missed its heartbeat. The outcome is: no restart and ACTION INHIBITED reported (true), or the task restarted (false).

| Case | Test | A: query configured | B: query says inhibited | Outcome |
|---|---|---|---|---|
| 1 | `test_mcdc_MC3_case1_A1_B1_inhibited_no_restart` | T | T | T |
| 2 | `test_mcdc_MC3_case2_A0_B1_no_query_restarts` | F | – | F |
| 3 | `test_mcdc_MC3_case3_A1_B0_not_inhibited_restarts` | T | F | F |

Independence: **A by (1, 2)**, **B by (1, 3)**. Clang measured 100 %. Mutation: A makes the suite crash on the NULL query (case 2), which counts as detected; B is killed by case 3 and 4 older tests.

## MC-4: mode machine T02, NOMINAL to SAFE on a critical fault, `mode_sm.c` (generated)

`(pending & BIT(FAULT_CRITICAL)) != 0U && (!in->fdir_inhibited)`, evaluated in NOMINAL when T01 (ground SAFE, priority 1) is not enabled. The outcome is whether T02 fires.

| Case | Test | A: fault_critical pending | B: not inhibited | Outcome |
|---|---|---|---|---|
| 1 | `test_mcdc_T02_case1_A1_B1_fires` | T | T | T |
| 2 | `test_mcdc_T02_case2_A0_B1_does_not_fire` | F | – | F |
| 3 | `test_mcdc_T02_case3_A1_B0_does_not_fire` | T | F | F |

Independence: **A by (1, 2)**, **B by (1, 3)**. Clang measured 100 %. Both mutations killed.

## MC-5: mode machine T05, refusal of an inhibited critical fault, `mode_sm.c` (generated)

`(pending & BIT(FAULT_CRITICAL)) != 0U && in->fdir_inhibited`, evaluated in NOMINAL only when T01 to T04 are not enabled. The outcome is whether the refusal (internal transition T05) fires.

| Case | Test | A: fault_critical pending | C: inhibited | Outcome |
|---|---|---|---|---|
| 1 | `test_mcdc_T05_case1_A1_C1_fires` | T | T | T |
| 2 | `test_mcdc_T05_case2_A0_C1_does_not_fire` | F | – | F |
| (3) | not constructible | T | F | – |

Independence: **A by (1, 2)**. **C cannot be shown, by construction.** With A true and C false, T02's guard (`A && !C`) is true. T02 has priority 2 and is tested first, so it fires and T05 is never evaluated. Case 3 cannot exist.

The mutation check confirms this. Replacing C with 1 is an **equivalent mutant**: no test, at any level, can tell the difference, because the code path where C would be false is never reached. The same holds for T06 (`fault_noncritical`, masked by T04) and T10 (masked by T08). Those are the three conditions clang reports as not covered in `mode_sm.c` (12 conditions, 9 covered).

The guard is kept as written, `&& in->fdir_inhibited`. Each transition's condition then reads exactly as in the model, and it stays correct if a model change ever reorders the priorities. This is the same argument as the Session 6 branch coverage justification, now with a mutant that demonstrates it.

## Tool-measured MC/DC, all flight modules, unit level

| Module | MC/DC conditions | Covered, first run | Covered, after the new tests |
|---|---:|---:|---:|
| `sp.c` | 8 | 8 | 8 |
| `pus.c` | 16 | 7 | **16** |
| `tc_handler.c` | 3 | 0 | **3** |
| `monitor.c` | 10 | 9 | **10** |
| `nvstate.c` | 5 | 5 | 5 |
| `supervisor.c` | 6 | 5 | **6** |
| `schedule.c` | 7 | 5 | **7** |
| `mode_sm.c` | 12 | 9 | 9 (3 by construction, above) |
| **Total** | **67** | **48 (71.6 %)** | **64 (95.5 %)** |

The first run is kept in `initial/`. Every gap in it was a defensive check that no test had triggered:

- NULL arguments in `pus.c` and `tc_handler.c`;
- a full service table;
- a table run without a callback in `monitor.c`;
- a ninth supervised task;
- a NULL telecommand in the schedule;
- two due activities whose slots are not in time order.

Each gap now has a test. The new tests are marked `test_s7_*` in the suites. The last gap was a real untested behaviour: the earliest due activity must be released first whatever its slot, which no test had shown before.
