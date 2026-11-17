# LOTUS-1 mission control (Yamcs, MDB, ground security unit, procedures)

From Lab 8.3, moved here for the integrated application.

```
Yamcs 5.13.7    --TC  :10025-->  bridge/sc_bridge.py          --TC: lines-->  spacecraft
(instance       <--TM :10015---  ground security unit:          <--TM: lines--  build/host/lotus1-test.host
 lotus1)                          keys, counters, challenge,                     or SIS (build/rtems/lotus1-test.exe)
 web :8090                        key generation and wrapping
                  raw uplink :10026 (no security unit; demo of a forged TC) ------>
```

| Path | What |
|---|---|
| `tools/gen_mdb.py` | Generates the MDB from one table of what the flight software sends and accepts (each table names the C function) |
| `yamcs/mdb/lotus1.xml` | The XTCE MDB (generated, do not edit): HK SID 1/2/3, 30 event layouts (thermal recovery and payload included), TM(1,x), TM(17,2), 19 commands (PAYLOAD included), alarms |
| `yamcs/etc/` | `yamcs.yaml`, `yamcs.lotus1.yaml` (links, PUS event decoder, MDB), `events.json` (generated templates), `command-queue.yaml` (supervised queue), `processor.yaml` |
| `bridge/sc_bridge.py` | Spacecraft link and ground security unit, plus the raw uplink port |
| `displays/operator.par`, `security.par` | Operator display: mode, health, payload, thermal recovery, key slots, KCVs, counters, rejections |
| `procedures/` | SAFE-01 recovery from SAFE: `.ycs` stack for Yamcs and `.md` with contingency C-SEC |
| `tools/check_mdb.py` | MDB against the ground decoders, field by field (`make mdb-check`: 5 930 comparisons, 0 mismatches) |
| `tools/yamcs_check.py` | End to end through Yamcs' REST API (`--rekey` adds a key change) |
| `tools/run_procedure.py` | Runs a `.ycs` stack from a script |
| `docs/mdb.md` | The database as tables |

## Running

Needs Java 17+ and Python 3 with `cryptography` (`sudo apt install openjdk-17-jre-headless python3-cryptography`).

```
mcs/run-bridge.sh fresh        # terminal 1: spacecraft (host simulation) + security unit
mcs/run-yamcs.sh               # terminal 2: first run downloads Yamcs 5.13.7 into mcs/.run/
# browser: http://localhost:8090  -> instance lotus1
python3 mcs/tools/yamcs_check.py --rekey     # optional: automatic end to end check
python3 demo/run_demo.py                     # the mission demonstration (demo/README.md)
```

On SIS instead of the host simulation: `RTEMS_PREFIX=... mcs/run-bridge.sh fresh sis`.

**First contact.** A fresh spacecraft (`fresh`, or any SIS start) is in factory key state and refuses commands until its counter floor is set. The bridge prints `FIRST CONTACT NEEDED`. In Yamcs: Commanding > Send a command > `RECOVERY_SET_COUNTER`, slot `SLOT_1`, floor `0`; then Commanding > Queues > supervised > accept it (critical commands wait there for a second operator). From then on every command works directly. `yamcs_check.py` and `run_demo.py` do this step by themselves.

## Design notes

- **Keys stay out of Yamcs.** The MDB's LOAD_KEY has one argument, the slot. The bridge generates the key, wraps it under the KEK (AES-128-CCM) and prints the key check value, which the operator compares with `sec_slotN_kcv` before ACTIVATE_KEY.
- **Recovery commands** (RECOVERY_SET_COUNTER, RECOVERY_ACTIVATE) are signed by the bridge with the recovery key and the challenge from the latest HK SID 3. A floor of 0 means "the security unit's safe floor" (above every counter value it has ever transmitted).
- **Verifiers.** Accepted and Complete match TM(1,1/2) and TM(1,7/8) by request ID (`org.yamcs.pus.Pus1Verifier`). SET_MODE has TM(1,7) as an execution stage and completes only when the HK mode equals the argument (NCR-01 from Session 7). A command rejected before authentication gets no TM(1,2): its Accepted stage ends in TIMEOUT, and the reason is in the security events.
- **Time.** Onboard time counts from boot, so Yamcs stamps packets with reception time (GenericPacketPreprocessor, `timestampOffset: -1`); `obt_coarse` / `obt_fine` are still decoded.
- **Lab only.** No Yamcs users, no TLS; the security unit's state (keys included) is a JSON file in `.run/`. See the ground segment note in `labs/session-08/lab-8.4-security-analysis/security-analysis.md`.
