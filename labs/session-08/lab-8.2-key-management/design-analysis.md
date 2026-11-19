# Lab 8.2 - Key management: design and failure analysis

## 1. Design

| Element | Implementation | Where |
|---|---|---|
| Key slots | 0: recovery key, in ROM, never loaded or erased. 1 and 2: operational, 32-octet HMAC-SHA-256 keys. One operational slot is ACTIVE. | `keystore.h` |
| Slot states | EMPTY, LOADED, ACTIVE, CORRUPT | `ks_state_t` |
| Anti-replay counter | One per operational slot, last accepted value, 32 bits, stored 3 times | `ks_slot_t.ctr[3]` |
| Recovery challenge | One 32-bit value, stored 3 times, published in HK SID 3, +1 after every use | `keystore_t.challenge[3]` |
| Key integrity | 3 copies of each key and of its key check value (KCV = first 4 octets of HMAC(key, "KCV")); scrub at 1 Hz | `ks_scrub()` |
| Rekey | TC(210,1) LOAD_KEY: slot, nonce, key wrapped with AES-128-CCM under a ROM key encryption key, associated data "KEY"+slot. Only into a slot that is not active. Authenticated under the active key, like every other command. | `ks_load()`, `sec_service.c` |
| Switch | TC(210,2) ACTIVATE: a LOADED slot becomes ACTIVE; the old one stays LOADED as the fallback | `ks_activate()` |
| Erase | TC(210,3): zeroise an inactive slot | `ks_erase()` |
| Key state in HK | SID 3, APID 0x022: active slot, slot states, KCVs, counters, challenge, rejection counters, scrub counters, flags. Never key material: the system test searches all telemetry for any 8-octet run of any key (case [8.2-3]). | `sec_app.c build_sid3()` |
| Persistence | The whole key store, with magic, version and CRC, after every accepted command and every key change. Host: a file. SIS: RAM (no NV). | `ks_encode()`, `persist()` |
| First contact | A blank or invalid NV image gives the factory state, in which slot 1's counter is **untrusted**: slot 1 refuses every command until TC(210,10), under the recovery key, sets a floor. Shown in SID 3 (flag) and by the bridge. | `ks_factory()`, `ks_counter_floor()` |

**Load and activate are separate.** The ground loads the new key, reads its KCV back in SID 3, compares it with the KCV the security unit computed (the bridge prints it), and only then activates. A key damaged in transit cannot pass the CCM tag (TC(210,1) then fails with TM(1,8) and KEY_LOAD_FAILED; test [8.2-1]). A key that is the wrong one, for example wrapped from the wrong ground record, is caught by the KCV comparison before it becomes the only key.

**Wrapped, not plain.** The new key crosses the RF link inside a telecommand. HMAC on the command protects its integrity, not its confidentiality: anyone receiving the uplink would read a plain key. AES-CCM under the key encryption key gives confidentiality, and binds the slot through the associated data (a key wrapped for slot 2 does not load into slot 1; unit test `test_load_rejects_bad_wrap`).

## 2. Four failure cases

### 2.1 A key corrupted by an upset

| Upset | Effect | Handling | Evidence |
|---|---|---|---|
| One copy of a key, or of its KCV | The MAC copy may be wrong until the next scrub: commands fail authentication for at most 1 s. | The scrub votes bit by bit across the 3 copies, checks the voted key against the voted KCV, rewrites all copies, emits KEY_REPAIRED. | `test_scrub_repairs_single_upset`; system [8.2-5] |
| The same bit in two copies of the ACTIVE key | The vote gives the wrong key; its KCV does not match. | The slot becomes CORRUPT (never used again), KEY_CORRUPT event (high severity), SID 3 shows CORRUPT. Every command under that slot is now rejected (class STATE). | `test_scrub_detects_lost_key`; system [8.2-5] |
| Then | The ground can no longer command with the operational key. | RECOVERY_ACTIVATE (TC(210,11), recovery key) makes the LOADED fallback slot ACTIVE. Then ERASE the corrupt slot and LOAD a new spare. | `test_recovery_activate_spare`; system [8.2-6] |

The scrub runs at 1 Hz, so a double upset needs two independent hits in the same octet column within one second before the first is repaired. The residual case, both operational slots lost, is in Section 3 of the security analysis (R-03).

### 2.2 A clock reset

Nothing in the authentication depends on time. Freshness is a counter, and the recovery challenge is a counter. This was chosen for this case: onboard time in this software counts from boot (Session 4 design note), so after any reset, onboard time starts again at 0.

With a time-window scheme, a reset would either make every command look old (lock-out until the clock is set, and setting the clock is itself a command), or, if the window were relaxed after a reset, re-open old commands to replay. With counters, the reset changes nothing: the counter comes back from NV (system test [8.1-6]: counter 107 restored, a packet captured before the reset is rejected after it).

A reset that also loses the NV is different, and was the gap this analysis found: the key store comes back in factory state, with the provisioned key and a counter of 0, so every command ever sent under that key would be accepted again. The spacecraft cannot tell a first boot from a lost NV, so it treats both the same way: no counter is trusted until the ground sets a floor above its high-water mark with the recovery key (first contact; unit `test_first_contact_after_factory`, system [8.2-0]). The cost is one recovery command at every factory boot.

What a clock reset does affect is time-tagged commanding: the Session 5 schedule releases at "seconds since boot", and the schedule is in RAM. Commands already released were authenticated at uplink (TC(11,4)). The schedule's contents are not persisted, so a reset loses them; that is a Session 5 limitation, not a security one.

### 2.3 A counter corrupted high

**Cause.** Either an upset in two of the three copies (the vote then takes the largest copy, by design: it can only err upwards), or a ground error that sent a value far ahead and had it accepted, or a counter close to 2^32.

**Effect.** Every command is now a REPLAY. The spacecraft is safe (nothing runs) but not commandable with that key. HK SID 3 shows the counter and the rising REPLAY count.

**Handling.** RECOVERY_SET_COUNTER (TC(210,10), recovery key, current challenge) sets the counter of a slot to a floor supplied by the ground. The floor must be **above every value the ground ever used** for that slot: then every command ever recorded on the uplink stays a replay. The security unit keeps that high-water mark (`safe_floor()`), and the bridge fills it in when the operator leaves the floor at 0. Unit test `test_recovery_reset_counter`, system test [8.2-4] (counter forced to 0xFFFFFF00, recovered, normal commanding restored, the recovery command itself rejected when replayed).

**Why the window rule matters here.** A counter that jumps too far ahead is rejected (WINDOW class, 65 536 steps), so a single ground mistake cannot push the counter near 2^32 and burn the key's whole counter space; and an attacker, who cannot produce a valid MAC anyway, gains nothing from it.

### 2.4 A reduced recovery command set: for or against?

**Position: for, with a minimal set, under a separate key.** It is implemented that way.

| Recovery set | Exactly two commands: set a slot's counter to a ground floor (210,10); activate a LOADED spare slot (210,11). |
|---|---|
| Key | Slot 0, in ROM, separate from the operational keys. Never used for anything else, so it is not exposed by routine traffic. |
| Freshness | A challenge published in HK SID 3, +1 after each use. Independent of the operational counters and of the clock, which are exactly what may be broken when recovery is needed. |
| Authorisation | The recovery key is refused for any other command, before execution (class STATE). Tested with TC(8,1) SAFE under the recovery key: refused, mode unchanged. |
| Not in the set | Mode changes, payload commands, key loading, erasing. |

**For.** Without it, two credible single events (a counter corrupted high, the active key lost to a double upset with no usable path to the fallback) leave a spacecraft that is healthy but no longer commandable: an availability loss caused by the security function itself, and permanent. A recovery set turns those into a procedure.

**Against, and how each point is answered.**

- *It is a second way in.* Yes, which is why it can do little: it cannot change modes, load keys or run payload commands. Its real power is stated plainly here: an attacker holding the recovery key can **lower** a counter, and then replay operational commands recorded off the uplink (they all carry values above the new floor). That is the price of being able to undo a counter corrupted high, since the spacecraft cannot tell a legitimate lowering from a malicious one. Two mitigations: every use is loud (RECOVERY_USED and COUNTER_SET events, the challenge moves in SID 3), and the recovery procedure ends with a rekey, after which every recorded command is under a key the spacecraft no longer accepts.
- *The ROM key cannot be changed.* True. Its compromise is permanent, so its custody matters more than the operational keys': it stays in the ground HSM and is used only by procedure. A longer term answer is a recovery key that can be replaced under itself; that adds a load path to the recovery set, which is what this design avoids.
- *The challenge is visible in TM.* It is meant to be. It is not a secret, only a one-time value: a recorded recovery command carries an old challenge and is rejected. What the downlink being unauthenticated does allow is a spoofed challenge: the ground would then sign recovery commands the spacecraft rejects. That costs recovery availability, not authority, and is recorded under R-01 in the security analysis.
- *Why not let the operational key reset counters?* Because the cases that need recovery are those in which the operational key is unusable.

What this does **not** cover is a loss of both operational slots: the recovery set does not load keys, so that case is mission loss of commanding (residual risk R-03, Section 3 of the security analysis). Allowing a recovery key load would cover it, at the price of making the recovery key able to install an attacker's key. This lab keeps the smaller set and accepts R-03, mitigated by keeping a LOADED fallback at all times and reloading a spare as soon as one slot is lost.
