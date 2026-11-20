# Test architecture

The tests of the integrated application (`src/`, listed in `src/sources.mk`) live here, by level. Evidence
goes to `../evidence/`.

| Level | Folder | What runs | Where | Catches |
|---|---|---|---|---|
| Static | `support/static_analysis.py`, `make warnings` | every flight source × 2 builds: GCC max warnings, clang `-Weverything`, cppcheck + MISRA, clang analyser, GCC `-fanalyzer` | host | coding defects, undefined behaviour, standard deviations |
| Unit | `unit/` | 9 Unity suites, one per module: `test_sp`, `test_pus`, `test_monitor`, `test_liveness`, `test_schedule`, `test_mode_sm`, `test_thermal`, `test_keystore`, `test_tc_auth` (142 tests) | host, no RTEMS | logic inside one module; MC/DC cases of the FDIR guards |
| Integration | `integration/` | `int_tc_path.c`: the real TC path (security, handler, services, FDIR, generated mode machine, schedule) linked together, TM captured at the sink (16 cases) | host simulation of RTEMS, and RTEMS 6 on SIS | interfaces between modules, ordering of reports, locking between TC_RX and SCHED, the security path |
| Security system | `security/` | `test_auth_system.py`: the 58 Session 8 checks (authentication, replay, window, rate-limited reporting, rekey, recovery key, key upsets, damaged wrapped key), migrated from `labs/session-08` and run against the integrated test build | host simulation (CI), SIS (`scripts/run-sis.sh security`) | the security boundary end to end, ground security unit included |
| Validation | `validation/` | `run_campaign.py`: the complete application driven only through its ground interface, every TC authenticated by the ground security unit | host simulation, and SIS | behaviour against the requirements, as the operator sees it |
| Scenario | `../demo/scenario.py` | the 7-step mission demonstration, automated (`make scenario` at the root) | host simulation (CI), SIS | the operational sequence end to end |
| Support | `support/` | Unity 2.6.0 (vendored), image runner with a termination marker, coverage, MC/DC, static analysis, tool pinning, RTEMS toolchain build | | |

**One entry point:** `make test` runs warnings, unit, integration, security and validation on the host, and returns non-zero if any of them fails. The other targets are listed at the top of the `Makefile`.

**Self-checking target images.** `int_tc_path` prints one line per case, then `@@ INTEGRATION RESULT: PASS n/n @@`, and shuts down. `support/run_image.py` waits for that marker, under a timeout, stops the simulator, and turns the marker into an exit code. The validation campaign does the same through its verdicts. No test needs a human to read a console.

**Testability.** Every core module (packet layer, handler, monitor, supervisor, watchdog, NV record, schedule, thermal recovery, mode machine, key store, authentication) builds for the host unit tests unchanged, with no RTEMS dependency. The application layer (`src/app/`, `fdir.c`, `security.c`) is RTEMS-bound, so it is tested from integration up. On the host it runs on the simulation of the RTEMS API (`src/sim/`); on the target it runs on RTEMS itself.

**History.** The suites in `labs/session-0x/*/tests` are kept as submitted. The suites here are their Unity ports plus the new tests, and are the maintained copy.
