# flight-sw-truong

[![fast-checks](https://github.com/Truongtm-2604mJ/flight-sw-truong/actions/workflows/fast-checks.yml/badge.svg)](https://github.com/Truongtm-2604mJ/flight-sw-truong/actions/workflows/fast-checks.yml)
[![nightly-target](https://github.com/Truongtm-2604mJ/flight-sw-truong/actions/workflows/nightly-target.yml/badge.svg)](https://github.com/Truongtm-2604mJ/flight-sw-truong/actions/workflows/nightly-target.yml)

Flight software for a small spacecraft on RTEMS 6 / LEON3 (SIS), built session by session in the Refonte Spacecraft Software Engineer Program.

| Session | Folder | Content |
|---|---|---|
| 3 | `labs/session-03`, `labs/lab-3.*` | RTEMS tasks, periodic scheduling, priority inversion, timing analysis |
| 4 | `labs/session-04` | CCSDS Space Packet, PUS services 1, 3, 5 and 17, housekeeping and events |
| 5 | `labs/session-05` | FDIR: monitoring, liveness supervision, watchdog, safe mode, fault injection |
| 6 | `labs/session-06` | Model-based development: the mode manager generated from a model, back-to-back tested |
| 7 | `labs/session-07`, `tests/`, `.github/workflows/` | Verification, validation and CI: test levels, coverage and MC/DC, static analysis, validation campaign |

## Tests

```
make -C tests test        # host: maximum warnings, unit (Unity), integration, validation campaign
make -C tests static      # static analysis, every finding dispositioned
make -C tests coverage    # gcov/gcovr by module and level;  make -C tests mcdc  for MC/DC
make -C tests target RTEMS_PREFIX=<rtems 6 prefix>   # integration and validation on SIS
```

See `tests/README.md` for the test architecture, and `labs/session-07/docs/verification-report.md` for the evidence.
