/*
 * Lab 8.2 - Onboard key store: key slots, key check values, anti-replay
 * counters, key loading and activation, recovery challenge.
 *
 * Slots
 *   0  recovery key. In ROM (a constant here), never loaded, never erased.
 *      Usable only for the two recovery commands (lab-8.2 design-analysis.md).
 *   1  operational key slot \  one is ACTIVE; the other holds a spare key
 *   2  operational key slot /  or is EMPTY. Loaded by TC(210,1), wrapped.
 *
 * Every operational slot has its own anti-replay counter (the last accepted
 * value). Counters and the recovery challenge are stored three times and
 * read by majority vote, so a single memory upset is corrected. If no two
 * copies agree, the largest copy is taken. That can only err upwards (the
 * true value is one of the copies or below the largest), so a corrupted
 * counter never re-opens a used value; at worst it is too high and every
 * command is a replay, which the recovery command TC(210,10) fixes.
 *
 * Key material is never reported: housekeeping carries slot states, key
 * check values (first 4 octets of HMAC(key, "KCV")) and counters only.
 *
 * Portable C99, no RTEMS dependency. Not thread safe: the application calls
 * it under its telecommand lock.
 */
#ifndef KEYSTORE_H
#define KEYSTORE_H

#include <stddef.h>
#include <stdint.h>

#define KS_SLOTS        3u
#define KS_RECOVERY     0u
#define KS_KEY_LEN      32u   /* HMAC-SHA-256 key                         */
#define KS_MAC_LEN      16u   /* HMAC-SHA-256 truncated to 128 bits       */
#define KS_KEK_LEN      16u   /* AES-128 key encryption key (ROM)         */
#define KS_NONCE_LEN    13u   /* AES-CCM nonce of a wrapped key           */
#define KS_WRAP_TAG     16u   /* AES-CCM tag of a wrapped key             */
#define KS_WRAPPED_LEN  (KS_KEY_LEN + KS_WRAP_TAG)
#define KS_WINDOW       65536u /* largest accepted step of a counter      */

typedef enum {
    KS_EMPTY   = 0,
    KS_LOADED  = 1,   /* holds a key, not active                        */
    KS_ACTIVE  = 2,
    KS_CORRUPT = 3    /* key check value no longer matches: unusable    */
} ks_state_t;

typedef struct {
    uint8_t  key[KS_KEY_LEN];        /* the copy used for MACs            */
    uint8_t  key_copy[2][KS_KEY_LEN];/* two more, for the scrub's vote    */
    uint32_t kcv[3];        /* computed when the key was loaded, 3 copies */
    uint8_t  state;         /* ks_state_t                                */
    uint32_t ctr[3];        /* last accepted counter, three copies       */
} ks_slot_t;

typedef struct {
    ks_slot_t slot[KS_SLOTS];  /* slot[0]: state and counters unused     */
    uint8_t   active;          /* 1 or 2                                 */
    uint32_t  challenge[3];    /* recovery challenge, three copies       */
    uint32_t  scrub_errors;    /* keys lost: vote did not match the KCV  */
    uint32_t  scrub_repairs;   /* keys or KCVs corrected by the vote     */
    uint8_t   ctr_untrusted;   /* bit n: slot n's counter is not known to
                                  be above every value ever used (factory
                                  state). That slot refuses commands until
                                  TC(210,10) sets a floor: "first contact" */
} keystore_t;

/* First boot (blank or invalid NV): slot 1 holds the provisioned key and is
 * active, slot 2 is empty, counters 0, challenge 1. Slot 1's counter is
 * UNTRUSTED: blank NV may mean lost NV, and then every command ever sent
 * under the provisioned key would be accepted again. Until the ground sets
 * a floor with the recovery key (TC(210,10)), slot 1 refuses commands. */
void ks_factory(keystore_t *ks);

/* NV image: magic, version, the structure, CRC-16. Returns the length
 * written, or 0 if cap is too small. ks_decode returns 0 if the image is
 * valid (magic, version, CRC), -1 otherwise (ks untouched). */
size_t ks_encode(const keystore_t *ks, uint8_t *out, size_t cap);
int    ks_decode(keystore_t *ks, const uint8_t *in, size_t len);
#define KS_IMAGE_LEN (8u + sizeof(keystore_t) + 2u)

/* Majority read of a counter / of the challenge, repairing the copies.
 * 0: majority (a single upset is corrected). 1: no two copies agreed, the
 * largest was taken (report it). -1: slot has no counter. */
int  ks_counter(keystore_t *ks, uint8_t slot, uint32_t *out);
void ks_counter_set(keystore_t *ks, uint8_t slot, uint32_t value);
int  ks_challenge(keystore_t *ks, uint32_t *out);
/* 1 if the slot's counter is untrusted (factory state, see ks_factory). */
int  ks_counter_untrusted(const keystore_t *ks, uint8_t slot);
/* Set a floor from the ground: the counter becomes trusted. */
void ks_counter_floor(keystore_t *ks, uint8_t slot, uint32_t value);
void ks_challenge_set(keystore_t *ks, uint32_t value);

/* Key check value of a key: first 4 octets of HMAC-SHA-256(key, "KCV"). */
uint32_t ks_kcv(const uint8_t key[KS_KEY_LEN]);

/* 1 if the slot can authenticate: slot 0 always; 1..2 if LOADED or ACTIVE.
 * The key itself is checked against its key check value by ks_scrub(), at
 * 1 Hz, not on every command (timing budget, security-report.md S4). */
int ks_usable(const keystore_t *ks, uint8_t slot);

/* Truncated HMAC-SHA-256 of data under the slot's key. 0, or -1. */
int ks_mac(const keystore_t *ks, uint8_t slot, const uint8_t *data, size_t len,
           uint8_t mac[KS_MAC_LEN]);

/* Unwrap (AES-128-CCM under the ROM key encryption key, associated data
 * "KEY" + slot) and store a key in an operational slot that is not active.
 * Its counter restarts at 0, trusted (a new key has no history). 0 on success; -1 bad slot or slot active;
 * -2 the wrapped key does not verify (nothing stored). */
int ks_load(keystore_t *ks, uint8_t slot, const uint8_t nonce[KS_NONCE_LEN],
            const uint8_t wrapped[KS_WRAPPED_LEN]);

/* Make a LOADED, usable slot the active one; the previous active slot
 * becomes LOADED (kept as the fallback). 0, or -1. */
int ks_activate(keystore_t *ks, uint8_t slot);

/* Zeroise an operational slot that is not active. 0, or -1. */
int ks_erase(keystore_t *ks, uint8_t slot);

/* Scrub every loaded slot: bitwise majority vote of the three key copies
 * and of the three KCV copies, then the key check value of the voted key.
 *   matches      all copies rewritten (a single upset anywhere is repaired,
 *                counted in scrub_repairs if any copy differed)
 *   no match     the slot becomes CORRUPT and is never used again; counted
 *                in scrub_errors
 * Returns a bit mask of the slots found corrupt by this call (bit n =
 * slot n). */
unsigned ks_scrub(keystore_t *ks);

/* The keys in "ROM" (lab test values, see keys/lab-keys.json). */
extern const uint8_t ks_rom_recovery_key[KS_KEY_LEN];
extern const uint8_t ks_rom_kek[KS_KEK_LEN];
extern const uint8_t ks_rom_provisioned_key[KS_KEY_LEN];

#endif /* KEYSTORE_H */
