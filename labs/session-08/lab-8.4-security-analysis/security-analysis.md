# Lab 8.4 - Security analysis

Scope: the Session 8 flight software (Session 6 application, generated mode manager, authentication and key management) and the lab ground segment (Yamcs 5.13.7, the bridge acting as ground security unit). Attackers considered: anyone who can transmit on the uplink frequency or record the downlink; an insider or malware on an MCS workstation; plus the natural faults that security functions must survive (upsets, resets, ground mistakes).

## 1. Assets

| ID | Asset | Why it matters | Where |
|---|---|---|---|
| A1 | Commanding authority | Whoever commands can change modes, switch off the payload, drain the battery, end the mission | TC path, service 8/11/12/19/210 |
| A2 | Operational keys (slots 1, 2) | Authority over A1 | Onboard key store; ground security unit |
| A3 | Recovery key (slot 0) and key encryption key | Authority over the recovery set and over key loading; cannot be changed after launch | Onboard ROM; ground HSM |
| A4 | Anti-replay counters and recovery challenge | Without them, a recorded command can be replayed | Onboard NV; ground security unit state |
| A5 | Availability of commanding | The security function can itself lock the ground out | All of the above |
| A6 | Telemetry integrity and confidentiality | Operators act on HK and events | Downlink (not protected in this lab) |
| A7 | Timing of the task set | Crypto runs on the same CPU as attitude control | TC_RX, TM_HK |

## 2. Threats, mitigations, evidence

| ID | Threat | Mitigation | Evidence |
|---|---|---|---|
| T1 | Forged command (no key) | HMAC-SHA-256/128 over header, data, SPI and counter; constant-time compare (tinycrypt `_compare`); rejected before any semantic check, never executed | [8.1-2]: TC(8,1) SAFE under a wrong key, mode stays NOMINAL; 1-bit change anywhere rejected |
| T2 | Replay of a recorded command | Per-slot monotonic counter, persisted, advanced only after full acceptance | [8.1-3]; [8.1-6] replay across a reset |
| T3 | Delayed or reordered command | Counter must increase: a held-back command is rejected once a later one was accepted | [8.1-3] older value rejected |
| T4 | Counter exhaustion or jump ahead | WINDOW: at most 65 536 ahead; never jumps | [8.1-4] |
| T5 | Oracle: learning from the answers | No TM(1,2) for any rejection before or at authentication; events give the class only, aggregated, never per packet | `test_wrong_key_rejected_at_auth`: 0 TM; [8.1-5] 0 TM(1,x) for 400 bad TCs |
| T6 | Flooding the downlink or the event log through rejections | Counting on the TC path, reporting by the HK task: at most one FIRST and one SUMMARY per class per 10 s window | [8.1-5]: 400 invalid TCs, 4 classes, 8 events, none lost from the counts; flood of 60 s bounded |
| T7 | CPU exhaustion by flooding (each bad TC costs one HMAC) | TC_RX runs below the three periodic tasks (priority 40); the worst uplink rate costs 0.086 of CPU, all of it slack | Timing budget, `docs/timing/` |
| T8 | Key disclosure on the link during rekey | Keys travel wrapped (AES-128-CCM, KEK in ROM), slot bound in associated data | `test_load_unwraps_ground_key`, `test_load_rejects_bad_wrap` |
| T9 | Key disclosure in telemetry | HK carries states, KCVs and counters only | [8.2-3] |
| T10 | Wrong or damaged key installed | CCM tag; load and activate separate; KCV compared on ground before activation | [8.2-1] |
| T11 | Recovery key used to command | Refused for anything but 210,10 and 210,11 | [8.2-4] TC(8,1) under SPI 0 refused |
| T12 | Recovery command replayed | One-time challenge, +1 per use | [8.2-4] |
| T13 | Scheduled commands as a bypass | TC(11,4) itself is authenticated; its inner TC is released "trusted" without a trailer | system test, schedule case |
| T14 | MCS compromise (an insider sends valid commands) | Out of scope of the onboard function: anything the ground signs is accepted. Mitigated in the ground segment: keys only in the security unit, never in Yamcs; critical commands (service 210) in a supervised queue needing a second operator | Section 5 |
| T15 | Replay after NV loss (counter back to 0) | Factory state: no counter trusted until the ground sets a floor | [8.2-0] |

## 3. Residual risks

| ID | Risk | Likelihood / impact | Why accepted, or what would close it |
|---|---|---|---|
| R-01 | Downlink not authenticated or encrypted: spoofed or read TM | Medium / medium | Out of the lab scope (uplink only). Close with SDLS on the TM frames. An operator deceived by forged TM could still only send authenticated commands. **Recovery availability:** the recovery challenge reaches the ground through this same TM. A spoofed SID 3 can present a false challenge, so the security unit signs recovery commands that the spacecraft rejects (REPLAY or WINDOW), and recovery fails while the spoofing lasts. This gives the attacker neither the recovery key nor any commanding authority (the false challenge only spoils a MAC the ground computes), but it can deny recovery. Mitigations within the current design: take the challenge only from SID 3 packets received in a pass with a consistent sequence count and onboard time, and if a recovery command is not answered, re-read the challenge and retry; a rejected recovery attempt does not consume the onboard challenge. Closed properly by authenticating the downlink (SDLS), not by changing the recovery set. |
| R-02 | **NV loss returns the key store to factory state**: slot 1 counter 0, so every command ever recorded under the provisioned key would be valid again | Low / high | **Closed in the design** (found during this analysis): in factory state slot 1 refuses every command (STATE) until a recovery command sets its floor above the ground high-water mark ("first contact"). Unit `test_first_contact_after_factory`, system [8.2-0]. Cost: every factory boot, the very first included, needs the recovery key once. |
| R-03 | Both operational slots lost (double upsets in both, or one lost while the other is EMPTY) | Very low / mission loss of commanding | The recovery set does not load keys (Section 2.4 of the design analysis). Mitigation: always keep a LOADED fallback; reload a spare as soon as one slot is lost. |
| R-04 | Recovery key or KEK compromised | Very low / high, permanent | In ROM, so cannot be rotated. Custody in the ground HSM, use by procedure only, every use reported (RECOVERY_USED). |
| R-05 | A command rejected semantically after authentication can be replayed and gets a TM(1,2) each time | Low / low | The counter only moves after full acceptance (lab requirement); the reply is to a sender who proved the key once. Bounded by the uplink rate. |
| R-06 | Scheduled commands sit in RAM unauthenticated until release | Low / medium | An attacker who can write onboard RAM has already won. Re-verifying at release would need the counter state of the past. |
| R-07 | Counter persisted on every accepted command: NV wear | Certain / low in the lab | About 10^5 writes a year at 300 commands a day. On flight NV, persist a reserved value ahead (counter + 64) and write once per 64 commands; after a reset the onboard counter is at most 64 too high, never too low. |
| R-08 | Third-party crypto code | Low / high | tinycrypt (Intel, archived, BSD). UBSan found undefined shifts in `aes_encrypt.c` (two, patched and recorded in `third_party/tinycrypt/VERSION`). Known-answer tests against Python hashlib and `cryptography` pin the behaviour. A flight project would use a qualified library or one with a maintained security record. |
| R-09 | Timing side channel on the MAC check | Very low / medium | Constant-time compare. HMAC time depends on length only. |
| R-10 | Lab keys are public | Certain / none in the lab | Marked in the code and in `keys/lab-keys.json`. |

## 4. Availability: can security lock us out?

Every way the security function can stop the ground commanding a healthy spacecraft, and the way back. None needs a working operational key.

| Lock-out path | Seen in | Way back | Tested |
|---|---|---|---|
| Ground counter behind onboard (ground state lost) | REPLAY count rises; SID 3 counter | Ground reads SID 3 and jumps its counter above it (the bridge does it automatically) | bridge `on_sid3` |
| Ground counter too far ahead | WINDOW count rises | Same | [8.1-4] |
| Onboard counter corrupted high | REPLAY; SID 3 | RECOVERY_SET_COUNTER | [8.2-4] |
| Counter copies disagree | COUNTER_VOTE event | None needed: largest copy taken (fails safe), at worst the case above | `test_counter_vote` |
| Active key lost | KEY_CORRUPT; SID 3 CORRUPT; STATE count | RECOVERY_ACTIVATE of the fallback | [8.2-5], [8.2-6] |
| Wrong key activated | All commands AUTH-rejected after the switch | Prevented by the KCV check before ACTIVATE; else RECOVERY_ACTIVATE of the fallback | design analysis 2.1 |
| NV blank after a reset | FACTORY flag and event; SID 3 "slot untrusted" | First contact: RECOVERY_SET_COUNTER with the ground high-water mark (R-02) | [8.2-0] |
| Both slots lost | KEY_CORRUPT twice | None (R-03) | - |
| CPU starved by a flood | - | Cannot happen: TC_RX is below the periodic tasks | timing budget |

The commanded mode transition itself is unaffected: SAFE entry by the FDIR needs no command, and the SAFE recovery procedure (SAFE-01) starts by checking that authenticated commanding works and branches to these recoveries if not.

## 5. Ground segment note

- **Where the keys live.** Only in the security unit (here `bridge/sc_bridge.py` with `ground/secunit.py`; in operations, an HSM in front of the uplink). Yamcs builds plain PUS commands and never sees a key, a wrapped key or a counter. LOAD_KEY in Yamcs carries only the slot number; the security unit generates and wraps the key. A compromised MCS workstation can send commands, which the security unit will sign (T14), but cannot extract keys.
- **Two-person rule.** All service 210 commands (and the test-only security injections) have significance "critical" in the MDB and go to the `supervised` queue, blocked until a second operator accepts them (`yamcs/etc/command-queue.yaml`). It costs nothing onboard.
- **What the operators see.** A command rejected before authentication gets no TM(1,2) by design: in Yamcs its Accepted stage ends in TIMEOUT. The explanation is in the security events (SEC_REJECT_FIRST with the class) and in SID 3 counters. Procedure SAFE-01 step 3 tells the operator this.
- **Lab shortcuts, not acceptable in operations.** Yamcs runs without users or TLS (`yamcs.yaml`); the security unit stores its state, keys included, in a JSON file; the bridge listens on localhost only. Each is listed in R-10 or here so that none is mistaken for the design.
- **MDB from the code.** The TM/TC database is generated from one table that names the C function that builds each packet, and `check_mdb.py` compares every field with the independent ground decoders (5 460 comparisons, 0 mismatches). A wrong MDB would make operators misread a key state.
