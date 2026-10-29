**Session 7 - Verification, Validation and CI** :white_check_mark:
Commit: <COMMIT_LINK>
Passing CI run: <CI_RUN_LINK>

**Headline coverage (host, unit + integration + validation, all flight code):** **96.3 %** statements (1474/1530), **86.1 %** branches; core modules (packet layer to mode machine) **98.1 % / 94.6 %**; all 56 lines not executed classified. **MC/DC (clang 18, unit level): 71.6 % first run -> 95.5 %**; the 3 remaining conditions are unreachable by construction (mutation check: the only surviving mutant is that equivalent one).

**7.1 Test architecture** (`tests/`): unit (Unity 2.6.0, 6 suites, 102 tests, no RTEMS), integration (13 cases: real TC path linked together, TM captured with a linker wrap; host sim + SIS, termination marker + timeout), validation (31 cases through the ground interface only), one entry point `make -C tests test`.
**7.2** Coverage per module and level with every uncovered line classified (5 real gaps found -> new tests, one was a requirement gap: PL_LOG escalation never tested at system level). Static analysis, 5 layers (GCC max warnings -Werror, clang -Weverything, cppcheck+MISRA, clang analyzer, GCC -fanalyzer): 129 findings, 7 fixed, 56 justified, 73 accepted with stated risk, 0 open — and honestly, **no runtime defect found by any analyser**.
**7.3 CI** (`.github/workflows/`): fast-checks every push (~4 min: 39 s of checks + runner setup), nightly-target (RSB 6.1 toolchain + SIS cached, runs integration + validation on SIS), release-evidence on tags (90-day archive). Actions pinned by SHA, tools by exact version (checked by `check_tools.sh`).
**7.4 Validation**: 31 requirements (7 TM/TC, 9 FDIR, 15 MM), 31 test cases through the ground interface only -> **30 PASS, 1 FAIL**, 0 untested; 3 campaign runs kept (26 -> 28 -> 29 PASS).
- **NCR-01** (TMTC-040, minor, **deferred**): TM(1,7) of TC(8,1) arrives up to 2 s before the mode changes. Workaround: wait for the TM(5,x) transition report; fix planned (deferred completion report).
- **NCR-02** (TMTC-030, minor, **fixed**): an over-long received line was dropped without being counted.
- **OBS-01** (validation finding, no requirement violated): the ground can go back to NOMINAL while an alarm is still active, with no reminder -> candidate requirement FDIR-100 for you/ops to decide.

Report: `labs/session-07/docs/verification-report.md`. Target (SIS) figures: `labs/session-07/validation/results-sis/` from the nightly job.
Actions quota: the fast job is ~4 min per push; the first nightly builds the RTEMS toolchain once (1-3 h) and caches it — if the repo is private this first build is the expensive part, I'll tell you if it hits the limit.
