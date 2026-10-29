# Session 7 - Software verification, validation and CI

The evidence for everything built in Sessions 4 to 6. Test code lives in `tests/` at the repository root (architecture: `tests/README.md`); the CI pipeline in `.github/workflows/`. This folder holds the reports.

Report: [`docs/verification-report.md`](docs/verification-report.md).

| Lab | Where | Deliverables |
|---|---|---|
| 7.1 | `tests/unit`, `tests/integration`, `tests/validation`, `tests/support` | 6 Unity suites (102 tests), integration suite (13 cases, host simulation and SIS), validation campaign (31 test cases), one entry point `make -C tests test` |
| 7.2 | `coverage/`, `static-analysis/` | Coverage by module and level, with HTML (`coverage/html/index.html`), every uncovered line classified, MC/DC tables, tool-measured MC/DC and mutation check; static analysis findings log with a disposition for each |
| 7.3 | `.github/workflows/` | `fast-checks` (every push), `nightly-target` (RTEMS toolchain cached, SIS), `release-evidence` (tags); every action and tool pinned |
| 7.4 | `validation/` | 31 requirements, test specification, results per run, NCR log, traceability matrix |

## Headline numbers (host simulation)

| | |
|---|---|
| Unit tests | 102, all pass |
| Integration tests | 13, all pass, 20 of 20 repeated runs |
| Statement / branch coverage, all levels, all flight code | 96.3 % (1474/1530) / 86.1 %, every one of the 56 lines not executed classified |
| Core modules (packet layer to mode machine) | 98.1 % statements, 94.6 % branches |
| MC/DC, unit level, tool measured | 71.6 % on the first run, 95.5 % now; the 3 conditions left are unreachable by construction |
| Static analysis | 129 distinct findings: 0 runtime defects; 7 fixed, 56 justified, 73 accepted with stated risk, 0 without disposition |
| Validation campaign | 31 test cases: 29 PASS, 1 FAIL (NCR-01), 1 OBSERVED; 31 requirements: 30 PASS, 1 FAIL, 0 untested |
| Nonconformances | NCR-01 (TMTC-040, deferred with workaround), NCR-02 (TMTC-030, fixed); 2 test anomalies; 1 validation observation |

## Folder map

```
coverage/          coverage.md, html/, mcdc.md, mcdc-clang-*.txt, mcdc-mutation.md,
                   justifications.yaml, initial/ (first runs)
static-analysis/   findings-log.md (triage), findings-current.md (generated),
                   dispositions.yaml, raw/ (tool outputs), initial/ (first run)
validation/        requirements.yaml, test-spec.md, results-host-sim/ (final run),
                   run1-host-sim/, run2-host-sim/ (earlier runs), ncr.yaml, ncr-log.md,
                   lower-level-tests.yaml, traceability-matrix.md
test-runs/         unit and integration outputs the matrix reads
docs/              verification-report.md
```

## Running

```
make -C tests test             # warnings, unit, integration, validation (host, about 10 min)
make -C tests static           # static analysis, fails on a finding without disposition
make -C tests coverage-full    # coverage, all levels (about 10 min); 'coverage' = unit + integration
make -C tests mcdc             # clang MC/DC + mutation check
make -C tests evidence         # copy test outputs, rebuild traceability matrix and NCR log
make -C tests target RTEMS_PREFIX=$HOME/Work/01.Code/rtems/rtems/6   # SIS
```
