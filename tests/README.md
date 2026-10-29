# Test architecture

The flight sources stay where each session built them (`labs/session-0x/`). The tests of the integrated flight software live here, by level:

| Level | Folder | What runs | Where | Catches |
|---|---|---|---|---|
| Static | `support/static_analysis.py`, `make warnings` | 16 flight sources × 2 builds: GCC max warnings, clang `-Weverything`, cppcheck + MISRA, clang analyser, GCC `-fanalyzer` | host | coding defects, undefined behaviour, standard deviations |
| Unit | `unit/` | 6 Unity suites, one per module: `test_sp`, `test_pus`, `test_monitor`, `test_liveness`, `test_schedule`, `test_mode_sm` (99 tests) | host, no RTEMS | logic inside one module; MC/DC cases of the FDIR guards |
| Integration | `integration/` | `int_tc_path.c`: the real TC path (dispatch, handler, services, FDIR, generated mode machine) linked together, TM captured at the sink (11 cases) | host simulation of RTEMS, and RTEMS 6 on SIS | interfaces between modules, ordering of reports, locking between TC_RX and SCHED |
| Validation | `validation/` | `run_campaign.py`: the complete application driven only through its ground interface (30 test cases, 31 requirements) | host simulation, and SIS | behaviour against the requirements, as the operator sees it |
| Support | `support/` | Unity 2.6.0 (vendored), image runner with a termination marker, coverage, MC/DC, static analysis, tool pinning, RTEMS toolchain build | | |

**One entry point:** `make test` runs warnings, unit, integration and validation on the host, and returns non-zero if any of them fails. The other targets are listed at the top of the `Makefile`.

**Self-checking target images.** `int_tc_path` prints one line per case, then `@@ INTEGRATION RESULT: PASS n/n @@`, and shuts down. `support/run_image.py` waits for that marker, under a timeout, stops the simulator, and turns the marker into an exit code. The validation campaign does the same through its verdicts. No test needs a human to read a console.

**Testability.** Every core module (packet layer, handler, monitor, supervisor, watchdog, NV record, schedule, mode machine) built for the host unit tests unchanged, with no RTEMS dependency. Sessions 4 to 6 kept RTEMS calls in the application layer (`app/`), and this is where that pays off. The application layer is RTEMS-bound, so it is tested from integration up: on the host it runs on the Session 5 simulation of the RTEMS API, and on the target on RTEMS itself.

**The legacy suites in `labs/session-0x/*/tests` are kept as submitted.** The suites here are their Unity ports (`support/port_to_unity.py`, same checks) plus the new tests; this is the maintained copy. The hand-written `modes.c` tests were not ported, because `modes.c` is no longer flown.
