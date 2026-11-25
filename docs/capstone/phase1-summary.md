# Capstone Phase 1: integration summary (for the mentor)

**Integration commit:** `1bacca8` (merge of PR #1). Follow-up commits on `main` are listed in section 4.
**Architecture document:** `docs/architecture.md`.

## 1. One application

Everything from Sessions 3 to 8 now lives in one source tree under `src/`, listed once in
`src/sources.mk`. The host simulation images (`build/host/lotus1-{test,flight}.host`) and the
RTEMS/LEON3 images for SIS (`build/rtems/lotus1-*.exe`) are built from that same tree by the same
`Makefile`. `labs/` is kept unchanged, as history only.

| Capability | Single owner | Duplicate removed |
|---|---|---|
| RTOS tasks | `src/app/main.c`, `src/app/mission.h` | Session 4 task set |
| TM/TC | one path, `src/app/tmtc.c`, then `src/core/tc_handler.c` (security hook, validation, dispatch), then `src/app/services.c` | Session 4 `tmtc_tasks.c`; the Session 8 linker-wrapped path |
| FDIR | `src/fdir/` | Session 5 `app/fdir.c` |
| Mode management | `src/fdir/mode_sm.c`, generated from `model/mode_machine.yaml`. The model, generator, trace and completeness report are kept together in `model/`, and CI checks that they agree. | hand-written Session 5 `modes.c` |
| Security | `src/security/` | linker wraps (`-Wl,--wrap`), replaced by 3 explicit calls |
| Mission services | `src/app/services.c`, `src/fdir/schedule.c` | none |
| Ground interface | `ground/` (one codec and one security unit), `mcs/` (Yamcs) | per-lab copies of the ground scripts |
| Host simulation | `src/sim/` (from Session 5) | Session 4 host simulation |

There is one command path:

ground → ground security unit → TC_RX → structure, CRC, MAC, freshness → semantic check → commit →
service → spacecraft function → TM(1,x) and events → TM_TX.

Scheduled commands enter only through an authenticated TC(11,4) (`docs/architecture.md` 3b).

## 2. Evidence preserved, and where each item went

| Evidence | Before | Now, against the integrated application | Result |
|---|---|---|---|
| Unit tests | 7 Unity suites (Session 7) and 2 security suites (Session 8) | `tests/unit/`, 9 suites. The security suites were migrated, and one test was added from the coverage analysis. | 142/142 |
| Integration tests | `int_tc_path` (Session 7) | `tests/integration/`. Security cases IT-15 and IT-16 and payload case IT-14 were added. | 16/16 on the host; on SIS, pass (IT-10 log in the SIS campaign) |
| Validation tests | 31 VTs (Session 7) | `tests/validation/run_campaign.py`, 35 VTs. New: payload, forged and replay, key upsets, thermal escalation. | host: 33 PASS, 1 FAIL known (NCR-01), 1 observation |
| Security system tests | `labs/session-08/.../test_auth_system.py`, 58 checks against the Lab 8 build | `tests/security/test_auth_system.py`, against `lotus1-test.host` (CI) and `lotus1-test.exe` on SIS | 58/58 on the host |
| MC/DC | Session 7, on `monitor.c`, `supervisor.c`, `mode_sm.c` | the same files, unchanged byte for byte, in `src/fdir/`. `make -C tests mcdc` runs in CI. | 9/10 mutants killed. The survivor is an equivalent mutant (argued in `evidence/coverage/mcdc.md`). |
| Coverage | Session 7 justifications | `evidence/coverage/`. Line numbers were re-mapped by source text, and the new security and thermal lines are justified or tested. | 96.6 % of lines, cumulative unit + integration + validation; 0 unclassified |
| Static analysis dispositions | Session 7 `dispositions.yaml` | `evidence/static-analysis/`, with file names updated. The 12 findings in files cppcheck had never seen were first held by SA-50 (open, OBS-03), then triaged one by one into SA-51 to SA-57; SA-50 is removed. | CI: 180 findings: fixed, justified or accepted with a stated risk, none open |
| Timing | Session 4 SIS, Session 8 SIS | `tools/timing/budget.py` covers the integrated task set: SUP, ACS, HK, LOG and SCHED. | Integrated SIS run (nightly CI): every R <= T, 0 missed periods. With the payload running (timing build): U = 0.383, PL_LOG R = 185 ms against 1000 ms, measured on the integrated application. |
| Yamcs procedures | Lab 8.3 | `mcs/procedures/` (SAFE-01, contingency C-SEC). The MDB is regenerated with the payload and thermal additions. | `make mdb-check`: 0 mismatches |
| Traceability | Session 6 and 7 matrices | `evidence/traceability-matrix.md`: 30 requirements, from requirement to design to code to tests | 29 PASS, 1 FAIL (TMTC-040, NCR-01) |

## 3. Kept visible

- **NCR-01 (deferred).** Not fixed and not hidden. VT-TMTC-040 still FAILs in every run. The MCS
  workaround (SET_MODE completes on the HK mode) is unchanged. CI accepts this failure only because an
  open or deferred NCR names the test, so any other failure still fails the job.
- **NCR-04 (open).** Key store persistence across a reset is shown on the host only. On SIS, [8.1-6]
  fails by design, and no claim of persistence on the target is made.
- **SIS is not QEMU.** SIS and QEMU run the same LEON3 BSP but are different simulators, so results from
  one are not results from the other. Neither is a board. Timing comes from SIS only; host timing is not
  used.

## 4. Integration findings (`evidence/ncr-log.md`)

| ID | Found | Status |
|---|---|---|
| NCR-03 | thermal recovery restarted after "exhausted" (SAFE is applied one supervisor cycle later) | major, fixed |
| NCR-05 | static helper names repeated across files (MISRA 5.9), found by CI's whole-program cppcheck | minor, fixed |
| TA-04 to TA-08 | test harness assumptions broken by integration, e.g. TA-08: on SIS every boot needs a first contact | fixed (no flight code changed) |
| OBS-03 | cppcheck has not yet been run on the Session 8 security files | open |

## 5. Regression results (host, CI)

| Level | Result |
|---|---|
| Build, maximum warning set, `-Werror`, both builds | 0 warnings |
| Unit | 142/142 |
| Integration | 16/16 |
| Security system tests | 58/58 |
| Validation campaign | 33 PASS, 1 FAIL (NCR-01, known), 1 observation |
| Mission scenario (7 steps) | 28/28 checks |
| Model check, MDB check | generated code matches the model; 0 mismatches |
| CI fast-checks on `main` | green |
| SIS, nightly-target CI (`evidence/sis-nightly-summary.md`) | timing: 0 missed periods; scenario 28/28; security tests 57/58 ([8.1-6] = NCR-04); integration PASS; validation 34/35 (VT-TMTC-040 = NCR-01) |
