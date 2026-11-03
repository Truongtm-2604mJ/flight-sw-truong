# Session 8 - Security in flight software and ground integration

The Session 6 flight software accepts only authenticated telecommands, manages its keys onboard, and is operated from Yamcs through a ground security unit.

Report: [`docs/security-report.md`](docs/security-report.md) (authentication, key management, recovery position, ground segment, timing budget).

| Lab | Folder | Deliverables |
|---|---|---|
| 8.1 | `lab-8.1-authentication/` | `tc_auth.c` (HMAC-SHA-256/128, anti-replay, check order), `sec_report.c` (rate limited rejection events), `tests/test_auth_system.py` (system tests), `tests/regression/` (Session 5 scenarios through the security unit) |
| 8.2 | `lab-8.2-key-management/` | `keystore.c` (3 slots, TMR, scrub, wrapped load, first contact), `sec_service.c` (service 210), `design-analysis.md` |
| 8.3 | `lab-8.3-ground-segment/` | Yamcs configuration, generated XTCE MDB, bridge / security unit, operator display, procedure SAFE-01, recording guide ([README](lab-8.3-ground-segment/README.md)) |
| 8.4 | `lab-8.4-security-analysis/` | `security-analysis.md`: assets, threats, mitigations, residual risks, availability, ground segment note |
| - | `app/` | The Session 6 application plus the security layer, attached by linker wraps; SIS script `run-sis.sh` |
| - | `ground/` | `secunit.py` (ground security unit), `seclink.py`, `secdecode.py`, `run_secure.py` |
| - | `docs/timing/` | Timing budget: estimate now, SIS measurement with `app/run-sis.sh` |
| - | `third_party/tinycrypt/` | Crypto primitives (v0.2.8, two UB fixes recorded in `VERSION`) |
| - | `keys/lab-keys.json` | **Lab test keys, public, worthless as secrets** |

## Telecommand format and check order

```
| primary 6 | PUS-C sec hdr 5 | app data | SPI 1 | SEQ 4 | MAC 16 | CRC 2 |
 \__________________ HMAC-SHA-256, first 16 octets ________/
```

structure -> CRC -> **authentication** -> **freshness** -> semantic -> commit counter -> execute. Rejections up to freshness: no TM(1,2), counted by class, reported by the HK task as at most one REJECT_FIRST and one REJECT_SUMMARY per class per 10 s.

## Results (host simulation)

| Check | Result |
|---|---|
| Unit tests, ASan + UBSan | 29/29 (`build/*.txt`) |
| Session 8 modules, flight warning set | 0 warnings |
| System tests, Labs 8.1 and 8.2 | 58/58 (`lab-8.1-authentication/tests/system-test.log`) |
| Lab 8.1 cases | valid TC executes, counter +1; wrong key: never executes, no TM; replay rejected, also across a reset; counter 65 537 ahead rejected (WINDOW); 400 invalid TCs -> 8 events, nothing lost |
| Session 5 scenarios through the security unit | gs_modes 49/51 and check_flight 11/11 (as Session 6); campaign 32/33: the corrupted-CRC TC no longer gets TM(1,2), by design (`lab-8.1-authentication/tests/regression/`) |
| MDB vs ground decoders | 5 460 field comparisons, 0 mismatches |
| Yamcs end to end | 19/19 incl. first contact and a rekey from Yamcs (`lab-8.3-ground-segment/logs/`) |
| SAFE-01 procedure | complete: SAFE (V_BUS injected) -> DEGRADED -> NOMINAL, schedule re-enabled |
| Sessions 4 to 7 after the handler hook | S4 tests, 13 integration, static analysis 129 findings all dispositioned: unchanged; Session 7 unit suite 102 -> 106 (4 tests of the hook's contract in `tests/unit/test_pus.c`) |
| Timing | estimate: U 0.381 -> 0.386; MAC +1.4 ms per TC; SIS: `app/run-sis.sh` |

## Running

Needs GCC, Python 3 with `cryptography` and `PyYAML`; Java 17+ for Yamcs.

```
make test                  # warnings, unit, MDB check, system tests (about 2 min)
make regression            # Session 5 scenarios through the security unit (about 8 min)
make rtems RTEMS_PREFIX=...                        # RTEMS 6 / LEON3 images
cd app && RTEMS_PREFIX=... ./run-sis.sh            # SIS: timing, budget, system tests
cd lab-8.3-ground-segment && ./run-bridge.sh fresh # then ./run-yamcs.sh in a second terminal
```

## Recovery command set: position

**For, minimal, under a separate ROM key.** Two commands: set a slot's counter floor (210,10) and activate a loaded spare (210,11). Fresh by a one-time challenge published in HK SID 3, independent of the counters and of the clock. The recovery key opens nothing else (mode commands under it are refused). Reasons and the price paid: `lab-8.2-key-management/design-analysis.md`, Section 2.4.
