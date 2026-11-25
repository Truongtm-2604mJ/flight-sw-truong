/* Lab 8.2 - Onboard key store. See keystore.h. */
#include "keystore.h"

#include <string.h>

#include "crc16.h"
#include <tinycrypt/aes.h>
#include <tinycrypt/ccm_mode.h>
#include <tinycrypt/constants.h>
#include <tinycrypt/hmac.h>

/*
 * LAB TEST KEYS. They are in a public repository, so they protect nothing;
 * they exist so the onboard software and the ground security unit
 * (ground/secunit.py, keys/lab-keys.json) agree. A flight unit gets its
 * recovery key and key encryption key at manufacture, from the key
 * management facility, into memory the software cannot rewrite.
 */
const uint8_t ks_rom_recovery_key[KS_KEY_LEN] = {
    0x17u, 0x3Du, 0x4Eu, 0x99u, 0xBEu, 0x34u, 0xA7u, 0x6Eu, 0x70u, 0xEAu,
    0x2Cu, 0x12u, 0xDEu, 0x98u, 0x93u, 0xD8u, 0xADu, 0xE4u, 0x81u, 0x9Bu,
    0x82u, 0x14u, 0xFAu, 0x2Eu, 0x1Eu, 0x71u, 0x87u, 0x91u, 0x88u, 0x97u,
    0xB8u, 0x9Eu
};
const uint8_t ks_rom_kek[KS_KEK_LEN] = {
    0x04u, 0x65u, 0x1Eu, 0xC4u, 0x88u, 0x44u, 0x40u, 0xB0u, 0x08u, 0x76u,
    0x55u, 0x6Du, 0xD1u, 0x18u, 0x1Fu, 0xF5u
};
const uint8_t ks_rom_provisioned_key[KS_KEY_LEN] = {
    0x52u, 0xE3u, 0xFDu, 0x74u, 0x2Fu, 0xC0u, 0xBBu, 0xDFu, 0x86u, 0x26u,
    0x23u, 0xF2u, 0x6Bu, 0x78u, 0x1Au, 0xF4u, 0xCBu, 0xDEu, 0x19u, 0x51u,
    0x17u, 0x38u, 0x45u, 0xE5u, 0xCEu, 0x7Bu, 0xFCu, 0x00u, 0x26u, 0x79u,
    0x8Du, 0x01u
};

#define KS_MAGIC   0x4B533038u   /* "KS08" */
#define KS_VERSION 1u

/* Overwrite that the compiler may not remove (dead store elimination). */
static void zeroise(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        v[i] = 0u;
    }
}

static void put_key(ks_slot_t *k, const uint8_t key[KS_KEY_LEN])
{
    uint32_t kcv = ks_kcv(key);
    (void)memcpy(k->key, key, KS_KEY_LEN);
    (void)memcpy(k->key_copy[0], key, KS_KEY_LEN);
    (void)memcpy(k->key_copy[1], key, KS_KEY_LEN);
    k->kcv[0] = kcv;
    k->kcv[1] = kcv;
    k->kcv[2] = kcv;
}

static int operational(uint8_t slot)
{
    return (slot >= 1u) && (slot < KS_SLOTS);
}

/* ---- Triple stored values ------------------------------------------------------ */

static int vote(uint32_t c[3], uint32_t *out)
{
    if (c[0] == c[1] || c[0] == c[2]) {
        *out = c[0];
    } else if (c[1] == c[2]) {
        *out = c[1];
    } else {
        /* No majority: take the largest copy. Never below the true value,
         * so no used counter value becomes acceptable again. */
        uint32_t m = c[0];
        m = (c[1] > m) ? c[1] : m;
        m = (c[2] > m) ? c[2] : m;
        *out = m;
        c[0] = m; c[1] = m; c[2] = m;
        return 1;
    }
    c[0] = *out;                 /* repair the minority copy */
    c[1] = *out;
    c[2] = *out;
    return 0;
}

int ks_counter(keystore_t *ks, uint8_t slot, uint32_t *out)
{
    if (!operational(slot)) {
        return -1;
    }
    return vote(ks->slot[slot].ctr, out);
}

void ks_counter_set(keystore_t *ks, uint8_t slot, uint32_t value)
{
    if (operational(slot)) {
        ks->slot[slot].ctr[0] = value;
        ks->slot[slot].ctr[1] = value;
        ks->slot[slot].ctr[2] = value;
    }
}

int ks_counter_untrusted(const keystore_t *ks, uint8_t slot)
{
    return operational(slot) && (ks->ctr_untrusted & (1u << slot)) != 0u;
}

void ks_counter_floor(keystore_t *ks, uint8_t slot, uint32_t value)
{
    if (operational(slot)) {
        ks_counter_set(ks, slot, value);
        ks->ctr_untrusted &= (uint8_t)~(1u << slot);
    }
}

int ks_challenge(keystore_t *ks, uint32_t *out)
{
    return vote(ks->challenge, out);
}

void ks_challenge_set(keystore_t *ks, uint32_t value)
{
    ks->challenge[0] = value;
    ks->challenge[1] = value;
    ks->challenge[2] = value;
}

/* ---- MAC and key check value ---------------------------------------------------- */

static int hmac(const uint8_t key[KS_KEY_LEN], const uint8_t *data, size_t len,
                uint8_t mac[KS_MAC_LEN])
{
    struct tc_hmac_state_struct h;
    uint8_t full[32];
    int ok;

    ok = tc_hmac_set_key(&h, key, KS_KEY_LEN) == TC_CRYPTO_SUCCESS &&
         tc_hmac_init(&h) == TC_CRYPTO_SUCCESS &&
         tc_hmac_update(&h, data, (unsigned int)len) == TC_CRYPTO_SUCCESS &&
         tc_hmac_final(full, (unsigned int)sizeof(full), &h) == TC_CRYPTO_SUCCESS;
    if (ok) {
        (void)memcpy(mac, full, KS_MAC_LEN);  /* truncated to 128 bits */
    }
    zeroise(&h, sizeof(h));
    zeroise(full, sizeof(full));
    return ok ? 0 : -1;
}

uint32_t ks_kcv(const uint8_t key[KS_KEY_LEN])
{
    static const uint8_t label[3] = { 0x4Bu, 0x43u, 0x56u };  /* "KCV" */
    uint8_t mac[KS_MAC_LEN];

    if (hmac(key, label, sizeof(label), mac) != 0) {
        return 0u;
    }
    return ((uint32_t)mac[0] << 24) | ((uint32_t)mac[1] << 16) |
           ((uint32_t)mac[2] << 8) | (uint32_t)mac[3];
}

int ks_usable(const keystore_t *ks, uint8_t slot)
{
    if (slot == KS_RECOVERY) {
        return 1;
    }
    if (!operational(slot)) {
        return 0;
    }
    return ks->slot[slot].state == (uint8_t)KS_LOADED ||
           ks->slot[slot].state == (uint8_t)KS_ACTIVE;
}

int ks_mac(const keystore_t *ks, uint8_t slot, const uint8_t *data, size_t len,
           uint8_t mac[KS_MAC_LEN])
{
    if (!ks_usable(ks, slot)) {
        return -1;
    }
    if (slot == KS_RECOVERY) {
        return hmac(ks_rom_recovery_key, data, len, mac);
    }
    return hmac(ks->slot[slot].key, data, len, mac);
}

/* ---- Life cycle ------------------------------------------------------------------- */

void ks_factory(keystore_t *ks)
{
    (void)memset(ks, 0, sizeof(*ks));
    put_key(&ks->slot[1], ks_rom_provisioned_key);
    ks->slot[1].state = (uint8_t)KS_ACTIVE;
    ks->slot[2].state = (uint8_t)KS_EMPTY;
    ks->active = 1u;
    ks->ctr_untrusted = (uint8_t)(1u << 1);   /* until the first contact */
    ks_challenge_set(ks, 1u);
}

int ks_load(keystore_t *ks, uint8_t slot, const uint8_t nonce[KS_NONCE_LEN],
            const uint8_t wrapped[KS_WRAPPED_LEN])
{
    struct tc_aes_key_sched_struct sched;
    struct tc_ccm_mode_struct ccm;
    uint8_t n[KS_NONCE_LEN];
    uint8_t key[KS_KEY_LEN];
    uint8_t aad[4];
    int ok;

    if (!operational(slot) || slot == ks->active) {
        return -1;
    }
    aad[0] = 0x4Bu; aad[1] = 0x45u; aad[2] = 0x59u;   /* "KEY" */
    aad[3] = slot;                  /* a key wrapped for slot 1 fails in 2 */
    (void)memcpy(n, nonce, sizeof(n));
    ok = tc_aes128_set_encrypt_key(&sched, ks_rom_kek) == TC_CRYPTO_SUCCESS &&
         tc_ccm_config(&ccm, &sched, n, KS_NONCE_LEN, KS_WRAP_TAG)
             == TC_CRYPTO_SUCCESS &&
         tc_ccm_decryption_verification(key, KS_KEY_LEN, aad, sizeof(aad),
                                        wrapped, KS_WRAPPED_LEN, &ccm)
             == TC_CRYPTO_SUCCESS;
    zeroise(&sched, sizeof(sched));
    if (!ok) {
        zeroise(key, sizeof(key));
        return -2;                  /* nothing stored, slot unchanged */
    }
    put_key(&ks->slot[slot], key);
    zeroise(key, sizeof(key));
    ks->slot[slot].state = (uint8_t)KS_LOADED;
    ks_counter_floor(ks, slot, 0u);           /* a new key has no history */
    return 0;
}

int ks_activate(keystore_t *ks, uint8_t slot)
{
    if (!operational(slot) || slot == ks->active ||
        ks->slot[slot].state != (uint8_t)KS_LOADED) {
        return -1;
    }
    if (operational(ks->active) &&
        ks->slot[ks->active].state == (uint8_t)KS_ACTIVE) {
        ks->slot[ks->active].state = (uint8_t)KS_LOADED;  /* the fallback */
    }
    ks->slot[slot].state = (uint8_t)KS_ACTIVE;
    ks->active = slot;
    return 0;
}

int ks_erase(keystore_t *ks, uint8_t slot)
{
    if (!operational(slot) || slot == ks->active) {
        return -1;
    }
    zeroise(ks->slot[slot].key, KS_KEY_LEN);
    zeroise(ks->slot[slot].key_copy, sizeof(ks->slot[slot].key_copy));
    zeroise(ks->slot[slot].kcv, sizeof(ks->slot[slot].kcv));
    ks->slot[slot].state = (uint8_t)KS_EMPTY;
    ks_counter_set(ks, slot, 0u);
    return 0;
}

unsigned ks_scrub(keystore_t *ks)
{
    unsigned found = 0u;
    uint8_t s;

    for (s = 1u; s < KS_SLOTS; s++) {
        ks_slot_t *k = &ks->slot[s];
        uint8_t voted[KS_KEY_LEN];
        uint32_t kcv;
        int differ = 0;
        size_t i;

        if (k->state != (uint8_t)KS_LOADED && k->state != (uint8_t)KS_ACTIVE) {
            continue;
        }
        for (i = 0; i < KS_KEY_LEN; i++) {
            uint8_t a = k->key[i], b = k->key_copy[0][i], c = k->key_copy[1][i];
            voted[i] = (uint8_t)((a & b) | (a & c) | (b & c));
            differ |= (a != b) || (a != c);
        }
        kcv = (k->kcv[0] & k->kcv[1]) | (k->kcv[0] & k->kcv[2]) |
              (k->kcv[1] & k->kcv[2]);
        differ |= (k->kcv[0] != k->kcv[1]) || (k->kcv[0] != k->kcv[2]);

        if (ks_kcv(voted) == kcv) {
            if (differ) {
                put_key(k, voted);        /* single upset: repaired */
                ks->scrub_repairs++;
            }
        } else {
            k->state = (uint8_t)KS_CORRUPT;   /* keeps its slot number but  */
            ks->scrub_errors++;               /* can never authenticate     */
            const unsigned bit = 1u << s; /* MISRA 10.7: no composite operand */
            found |= bit;
        }
        zeroise(voted, sizeof(voted));
    }
    return found;
}

/* ---- Non volatile image ------------------------------------------------------------- */

static void ks_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static uint32_t ks_get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

size_t ks_encode(const keystore_t *ks, uint8_t *out, size_t cap)
{
    uint16_t crc;

    if (cap < KS_IMAGE_LEN) {
        return 0u;
    }
    ks_put32(&out[0], KS_MAGIC);
    ks_put32(&out[4], ((uint32_t)KS_VERSION << 16) | (uint32_t)sizeof(keystore_t));
    (void)memcpy(&out[8], ks, sizeof(keystore_t));
    crc = crc16_ccitt(out, 8u + sizeof(keystore_t));
    out[8u + sizeof(keystore_t)] = (uint8_t)(crc >> 8);
    out[9u + sizeof(keystore_t)] = (uint8_t)crc;
    return KS_IMAGE_LEN;
}

int ks_decode(keystore_t *ks, const uint8_t *in, size_t len)
{
    uint16_t crc;
    keystore_t tmp;

    if (len < KS_IMAGE_LEN || ks_get32(&in[0]) != KS_MAGIC ||
        ks_get32(&in[4]) != (((uint32_t)KS_VERSION << 16) |
                          (uint32_t)sizeof(keystore_t))) {
        return -1;
    }
    crc = crc16_ccitt(in, 8u + sizeof(keystore_t));
    if (in[8u + sizeof(keystore_t)] != (uint8_t)(crc >> 8) ||
        in[9u + sizeof(keystore_t)] != (uint8_t)crc) {
        return -1;
    }
    (void)memcpy(&tmp, &in[8], sizeof(tmp));
    if (!operational(tmp.active)) {
        return -1;
    }
    *ks = tmp;
    zeroise(&tmp, sizeof(tmp));
    return 0;
}
