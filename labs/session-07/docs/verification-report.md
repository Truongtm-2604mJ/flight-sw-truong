# Verification report: flight software, Sessions 4 to 7

**Scope.** The flight software as flown from Session 6 on:

- the TM/TC stack (Session 4);
- FDIR (Session 5);
- the generated mode machine (Session 6, model v3);
- the application around them.

That is 16 C source files: the core modules, the application layer, and the fault injection used by test builds only.

**Environments.** Host build; host simulation of the RTEMS API; RTEMS 6 / LEON3 under SIS. The numbers in this report are from the host levels. The target runs use the same tests (`make -C tests target`, the nightly CI job), which write `validation/results-sis/`.

**Terms.**

- **Verification:** does the code do what its specification says? Unit, integration, static analysis, coverage.
- **Validation:** does the specified behaviour serve the mission? The campaign through the ground interface, and its observations.

## 1. Test architecture

| Level | Pairs with (V model) | What runs | Environment | Catches | Cannot catch |
|---|---|---|---|---|---|
| Static | Implementation | GCC max warnings `-Werror`; clang `-Weverything`; cppcheck + MISRA addon; clang analyser; GCC `-fanalyzer` | host, both builds | coding defects, undefined behaviour, standard deviations | anything that depends on what the code *should* do |
| Unit | Detailed design | 6 Unity suites, 102 tests, one per core module | host, no RTEMS | logic inside a module; MC/DC of the guards | interfaces, timing, the OS |
| Integration | Architecture | `int_tc_path`: real dispatch, handler, services, FDIR and mode machine linked together, TM captured at the sink with a linker wrap; 13 cases | host simulation of RTEMS; RTEMS 6 on SIS | report ordering, locks between the TC_RX and SCHED paths, service and FDIR interactions | UART framing, periodic tasks, real priorities |
| Validation | Requirements | 31 test cases through the ground interface only (TC lines in, TM lines out), on the test and the flight build | host simulation; SIS | behaviour against the 31 requirements, as an operator sees it | real timing, hardware, radiation |

**Why this split.** Every core module built for the host with no change. Sessions 4 to 6 kept RTEMS calls in the application layer, so testability was already an architectural property, not something retrofitted.

The application layer (`tmtc5.c`, `services.c`, `fdir6.c`, `lab5.c`) is bound to RTEMS. It is therefore tested from integration up, where the Session 5 host simulation provides the RTEMS API, and on the target, where RTEMS itself does.

Each defect class is tested in the cheapest environment that can show it:

- logic, on the host (milliseconds);
- interfaces, in the simulation (under 1 s);
- requirements, through the ground link (8 minutes, most of it the 300 s false positive run).

**Automation.** One entry point, `make -C tests test`. Target images end with a marker (`@@ INTEGRATION RESULT: PASS 13/13 @@`), which `run_image.py` turns into an exit code under a timeout. The campaign returns its verdicts the same way. No test needs a person to read a console.

**Determinism.** The integration suite passed 20 consecutive runs. The one timing-dependent validation step, two telecommands that must land in the same 2 s cycle, detects when they split across cycles and retries (it landed on the first attempt in all three runs).

## 2. Coverage

gcov (GCC 13.3), gcovr 7.2. The levels are cumulative. Full table, HTML and uncovered lines: `coverage/coverage.md` and `coverage/html/index.html`.

| Module | Kind | Unit: lines | + integration: lines | + validation: lines | + validation: branches |
|---|---|---:|---:|---:|---:|
| `sp.c` | core | 100.0 % | 100.0 % | 100.0 % | 100.0 % |
| `crc16.c` | core | 100.0 % | 100.0 % | 100.0 % | 100.0 % |
| `pus.c` | core | 100.0 % | 100.0 % | 100.0 % | 100.0 % |
| `tc_handler.c` | core | 96.2 % | 96.2 % | 100.0 % | 100.0 % |
| `monitor.c` | core | 100.0 % | 100.0 % | 100.0 % | 100.0 % |
| `supervisor.c` | core | 100.0 % | 100.0 % | 100.0 % | 90.0 % |
| `watchdog.c` | core | 100.0 % | 100.0 % | 100.0 % | 100.0 % |
| `nvstate.c` | core | 100.0 % | 100.0 % | 100.0 % | 91.7 % |
| `schedule.c` | core | 100.0 % | 100.0 % | 100.0 % | 100.0 % |
| `mode_sm.c` | core (generated) | 94.0 % | 94.0 % | 94.0 % | 79.4 % |
| `tmtc5.c` | application | – | 24.1 % | 88.8 % | 62.9 % |
| `services.c` | application | – | 76.2 % | 98.0 % | 78.7 % |
| `fdir6.c` | application | – | 53.6 % | 97.1 % | 85.5 % |
| `sensors5.c` | application | – | 21.1 % | 100.0 % | 100.0 % |
| `lab5.c` | application | – | – | 87.0 % | 67.6 % |
| `finj.c` | test build only | – | 0.0 % | 93.9 % | 85.3 % |
| **Core modules** |  | **97.7 %** | **97.7 %** | **98.1 %** | **94.6 %** |
| **All flight code** |  | **97.7 %** | **74.4 %** | **96.3 %** | **86.1 %** |

Notes on the table:

- **Unit totals** count only the modules a unit suite builds (778 lines). The application layer appears from integration on, and `lab5.c` (the task bodies) only at validation.
- **All flight code at the last level:** 1474 of 1530 lines, and 86.1 % of branches.
- **`mode_sm.c`** includes its test-only name strings. The Session 6 back-to-back suites, measured on its flight code only, give 100 % of statements and 94.7 % of branches (`labs/session-06/lab-6.3-equivalence/results/coverage.md`).

**Uncovered lines.** 56 flight lines are not executed at any level, and 0 are unclassified. In the core modules the only lines missed are 15 name-string lines of `mode_sm.c`, which are test-only. Branch coverage of the application layer (62 to 86 %) is lower than its line coverage, because the branches not taken are mostly the error exits of the lines classified below.

Every remaining line is classified in `coverage/justifications.yaml`; the report fails on any unclassified line:

- **Not in the flight build:** the mode machine's name strings (test builds only); fault injection; host-simulation-only paths.
- **Defensive:** RTEMS object creation failures at boot; console read and write errors; a full TM queue, shown not to happen by VT-TMTC-050: 0 drops over 681 packets in 10 boots; the invalid-state hook, reachable only by memory corruption.
- **Unreachable by construction:** a duration check behind an acceptance check that already rejects the same values; the closing brace after `rtems_task_exit()`.

**Five gaps were real and now have tests (C-01 to C-05).** One of them was a requirement gap: no system test had exercised a payload task (PL_LOG) escalation, and FDIR-050 did not mention it. The first analysis is kept in `coverage/initial/`.

**MC/DC.** clang 18 measures MC/DC directly (`-fcoverage-mcdc`). At unit level, over the 67 conditions in compound decisions:

| Run | Conditions covered |
|---|---|
| First run | 48 (71.6 %) |
| After the new tests | **64 (95.5 %)** |

The gaps were untested defensive checks: NULL arguments, full tables, slot order in the schedule.

Five FDIR guard decisions have hand-built MC/DC tables with explicit independence pairs (`coverage/mcdc.md`). Summary for two of them:

| Decision | Conditions | Cases | Independence pairs |
|---|---|---|---|
| MC-1 monitor clear band: `v >= low_clear && v <= high_clear` | A, B | (T,T)=T, (F,–)=F, (T,F)=F | A: 1-2, B: 1-3 |
| MC-5 mode T05 refusal: `fault_critical && inhibited` | A, C | (T,T)=T, (F,–)=F | A: 1-2; **C impossible** |

For MC-5, A true and C false enables T02, which has higher priority, so T05 is never evaluated. This is masked by construction, the same for T06 and T10. These are the 3 conditions left in the 95.5 %.

A mutation check confirms the tables. Each condition was replaced by a constant, one at a time:

- 9 of 10 mutants were detected by the tests;
- the survivor is exactly MC-5 C, an equivalent mutant: no test can distinguish it, which is what "unreachable by construction" means.

## 3. Static analysis

| Layer | Distinct findings | Disposition |
|---|---:|---|
| GCC max warnings (`-Werror` gate) | 14 | all justified: `-Wunused-macros` on RTEMS configuration macros in the host build |
| clang `-Weverything` | 21 | justified: C++ buffer-hardening model, padding, `noreturn` on task bodies, defensive default |
| cppcheck + MISRA C:2012 addon | 85 | 73 accepted MISRA deviations with stated risk, 12 justified |
| clang analyser | 8 | justified: `memcpy_s` (Annex K) is not available in newlib |
| GCC `-fanalyzer` | 1 | justified: false positive, traced by hand |
| **Total** | **129** | **7 fixed (4 changes), 56 justified, 73 accepted, 0 open** |

**Fixed:**

- a bad function cast;
- a variable scope;
- four `if`/`else if` chains without an `else` (MISRA 15.7);
- hook parameter names.

**No tool found a runtime defect.** The defects of this session came from validation (NCR-02) and from coverage analysis.

Every finding is matched to a disposition in `static-analysis/dispositions.yaml`. A new finding fails CI. The triage reasoning is in `static-analysis/findings-log.md`.

This is not a MISRA compliance claim: the addon checks a subset of the rules, without rule texts, and no deviation was formally approved.

## 4. Validation campaign and traceability

There are 31 requirements:

- 7 TM/TC and 9 FDIR, new, written from the Session 4 and 5 design notes;
- the 15 mode management requirements of Lab 6.4.

Each has a validation test case (identifier, preconditions, steps, expected result) in `validation/test-spec.md`, generated from the same definitions the campaign executes. The exceptions are MM-100, verified by analysis (the generator's completeness check), and MM-130, verified at integration level because the ground cannot choose the evaluation cycle a command lands in.

| Run | Test cases | PASS | FAIL | Other | What changed after it |
|---|---:|---:|---:|---|---|
| 1 | 30 | 26 | 3 | 1 OBSERVED | NCR-01 and NCR-02 opened; TA-01 (test error) found |
| 2 | 30 | 28 | 1 | 1 OBSERVED | NCR-02 fixed and retested; TA-01 corrected |
| 3 (final) | 31 | 29 | 1 | 1 OBSERVED | VT-FDIR-050b added after the coverage analysis |

Final traceability (`validation/traceability-matrix.md`, generated from the test outputs):

- **31 requirements, 30 PASS, 1 FAIL (TMTC-040, NCR-01), 0 without a test.**
- 26 requirements have tests at two or more levels.
- The matrix lists every unit, integration, Session 6 and validation test with its verdict.

## 5. Nonconformance log

| ID | Kind | Requirement | Severity | Status | Summary |
|---|---|---|---|---|---|
| NCR-01 | NCR | TMTC-040 | minor | **deferred** | TM(1,7) of TC(8,1) is sent up to 2.0 s before the mode changes (measured 1.999 s). Workaround: procedures wait for the TM(5,x) transition report (MM-080, MM-150). The fix, a completion report deferred to the transition, changes the service 1 contract of every TC and is planned with its own tests. |
| NCR-02 | NCR | TMTC-030 | minor | **fixed** | An over-long received line was dropped without being counted. Fixed in `tmtc5.c`; retest PASS. |
| TA-01 | test anomaly | MM-030 | – | fixed | Test compared telemetry across boots; corrected, product unchanged. |
| TA-02 | test anomaly | TMTC-010/020 | – | fixed | Integration expectations contradicted the documented design (destination 0 for packet-level rejections); corrected. |
| TA-03 | test anomaly | integration | – | fixed | First SIS run of the integration image: RTEMS fatal 38, FPU used by a task without `RTEMS_FLOATING_POINT`. The harness, unlike the application, created its tasks without the attribute; the host simulation ignores attributes, so only the target showed it. Harness fixed; flight code unchanged. |
| OBS-01 | validation observation | none | – | open | After an alarm, the ground can return to NOMINAL while the alarm is still active, and nothing reminds it. No requirement is violated; candidate requirement FDIR-100 is raised for operations to decide. |

NCR-02 and OBS-01 illustrate the two halves of V&V:

- **NCR-02 is a verification failure:** the code did not do what its requirement said.
- **OBS-01 passes verification and raises a validation question:** the code does what MM-130 says, but MM-130 may not say enough for the mission.

The details are in `validation/ncr-log.md`.

## 6. Limitations: what was not verified

- **No independence.** One person wrote the code, the requirements and the tests. The requirements were written from design notes, not from code, and the mutation check tests the tests. Still, nobody "not anchored to the code's assumptions" has reviewed it, apart from the mentor's session reviews. For anything above the lowest criticality this would need ISVV.
- **The target numbers are not in this report.** Coverage, static analysis and the campaign figures are host measurements. SIS runs the same integration image and campaign (`make -C tests target`; `run-sis.sh` for the Session 6 scenarios). On SIS, NV memory does not survive a reset, so the boot-after-watchdog part of FDIR-060 is N/A there by design.
- **QEMU was not used.** The target since Session 3 is SIS (same LEON3 BSP, instruction-level simulator). SIS shows target compilation and RTEMS behaviour, but not real timing or peripherals.
- **Timing evidence is from Session 3.** WCET and the utilisation of 0.381 were not re-measured after Sessions 5 to 7 added the supervisor and the generated step; the step is small, but this is argued, not measured.
- **The host simulation runs tasks as threads without priorities.** Preemption and priority inheritance are not exercised there. On SIS, IT-11's two dispatching tasks have equal priority on one core, so the lock is exercised only at the tick boundary. That is a weak test of the tc lock.
- **MISRA coverage is partial** (above), and the essential type model is not enforced beyond GCC's conversion warnings.
- **CI.** The pipelines are defined and every step was run locally with the pinned tools. The first GitHub runs, and the first cold toolchain build (1 to 3 h, then cached), happen on the first push.

**What I would test next, given hardware.**

- On a LEON3 board (processor in the loop): WCET per task, the GPTIMER hardware watchdog instead of the software model, UART framing at the real baud rate, and NV persistence across a real reset (MRAM or flash).
- With hardware in the loop: the FDIR thresholds against real sensor noise, and a long soak (days) for false positives, against the 300 s here.
- EDAC and memory scrubbing, which Session 5 lists as detection mechanisms, could only be tested with fault injection into memory. That is not possible in SIS without its own scripting.
