# Lab 8.2 - Key management

| File | What |
|---|---|
| `keystore.h/.c` | Slots (0 recovery ROM, 1-2 operational), triple stored keys/KCVs/counters/challenge, scrub, AES-CCM unwrap, NV image, first contact |
| `sec_service.h/.c` | Service 210: LOAD_KEY (1), ACTIVATE (2), ERASE (3), RECOVERY_SET_COUNTER (10), RECOVERY_ACTIVATE (11); key events |
| `design-analysis.md` | Key upset, clock reset, counter corrupted high, NV loss, and the position on the reduced recovery set (for) |
| `../tests/unit/test_keystore.c` | 11 unit tests, reference values from the Python ground |

Key state in HK: SID 3 on APID 0x022 (`../app/sec_app.c build_sid3()`), 41 octets: active slot, slot states, KCVs, counters, recovery challenge, rejection counters, scrub counters, flags. Never key material.
