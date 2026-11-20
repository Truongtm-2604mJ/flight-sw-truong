# LOTUS-1 evidence set

Everything a reviewer needs to check the claims in the technical report
(`docs/report/LOTUS-1-technical-report.pdf`). Generated files say which script
wrote them; nothing here is typed by hand except the YAML sources marked
*source*.

| # | Evidence | Where | How it is produced |
|---|---|---|---|
| 1 | **Requirements** (30, six areas) | `requirements.yaml` *(source)* | written from the mission statement (`docs/mission.md`) and the Session 6/7 sets |
| 2 | **Traceability matrix**: requirement → design → code → tests at every level, with results | `traceability-matrix.md` | `make -C tests matrix` from `requirements.yaml`, `lower-level-tests.yaml` *(source)*, the test outputs below and `validation/results-*/results.json` |
| 3 | **Coverage** with a justification for every uncovered flight line | `coverage/coverage.md`, `coverage/justifications.yaml` *(source)*, `coverage/html/` (where gcovr is installed, as in CI) | `make -C tests coverage-full` (unit + integration + validation + scenario on instrumented builds) |
| 3b | MC/DC of the FDIR guards and a mutation check | `coverage/mcdc.md`, `coverage/mcdc-mutation.md` | `make -C tests mcdc` (clang 18) |
| 4 | **Static analysis** with a disposition for every finding | `static-analysis/findings-current.md`, `static-analysis/dispositions.yaml` *(source)*, `static-analysis/raw/` | `make -C tests static` |
| 5 | **Non-conformance log**: every defect found in integration and validation, severity, disposition; open items open | `ncr-log.md` (from `ncr.yaml` *(source)*) | `make -C tests matrix` |
| 6 | **Timing analysis** of the integrated task set | `timing/budget-estimate.md` (estimate), `timing/budget-sis.md`, `timing/sis-*.log` (SIS) | `tools/timing/budget.py`; on SIS `make sis` |
| 7 | Validation campaign: specification and results | `validation/test-spec.md`, `validation/results-host-sim/` (`results.md`, `results.json`, `campaign.log`), `validation/results-sis/` | `make -C tests validation`; on SIS `make -C tests target` |
| 8 | Unit, integration and security system test outputs | `test-runs/unit-results.txt`, `test-runs/integration-host-sim.log`, `test-runs/security-system-host-sim.log` (58/58), `test-runs/security-system-sis.log` | `make -C tests evidence security`; on SIS `make sis STEPS=security` |
| 9 | Mission scenario (the demonstration), automated | `demo/scenario-host-sim.log`, `.json`; `demo/scenario-sis.*` | `make scenario`; on SIS `make sis STEPS=demo` |

## Reading order for a reviewer

1. `requirements.yaml`, then `traceability-matrix.md`: the status column says PASS, FAIL (with its NCR) or
   NOT RUN (target evidence not produced yet) for every requirement.
2. `ncr-log.md`: what failed, why, and what is still open.
3. `coverage/coverage.md` and `static-analysis/findings-current.md`: the code-level evidence.
4. `timing/`: the response time analysis of the integrated task set.
5. `demo/` and `validation/results-host-sim/results.md`: what the system did, packet by packet.

## Environments

- **Host**: GCC 13.3, Python 3, the host simulation of the RTEMS API (`src/sim/`).
- **Target**: RTEMS 6.1, LEON3 BSP, SIS from the RTEMS Source Builder 6.1 (pinned commits in `.github/workflows/nightly-target.yml`).
- **CI**: GitHub Actions, `ubuntu-24.04`, tools pinned and checked by `tests/support/check_tools.sh`.
