# Session 5 scenarios through the ground security unit

`../../../ground/run-s5-regression.sh` runs the Session 5 ground scripts, unchanged, against the Session 8 test and flight builds. `run_secure.py` makes every TC they send an authenticated one; the scripts do not see APID 0x022. A fresh key store is commissioned first (first contact), then the spacecraft restarts cold, so each script sees the boot it expects.

| Script | Session 6 (no security) | Session 8 | Difference |
|---|---|---|---|
| `gs_modes.py` | 49/51 | 49/51 | none: the same two D-07 checks (cyclic mode evaluation) |
| `campaign.py` | 33/33 | 32/33 | **intended**: the corrupted-checksum row expects TM(1,2) code 3. A TC that fails before authentication now gets no answer (no oracle); the rejection is counted and reported as SEC_REJECT_FIRST class CRC. Not executed, as before. |
| `check_flight.py` | 11/11 | 11/11 | none (TC(200,x) refused by the flight build with TM(1,2) code 7: those TCs were authenticated, so they are answered) |

The campaign difference is a requirement change, not a regression: the Session 7 requirement that every rejected TC gets a failure report (TMTC-020) now applies only to authenticated telecommands. Logs: `gs_modes.log`, `campaign.log`, `campaign.md`, `check_flight.log`.
