/* Unit suite: key store (lab-8.2-key-management/keystore.c).
 *
 * Reference values (MAC, key check values, the wrapped key) come from the
 * ground implementation, Python hmac/hashlib and the 'cryptography' AES-CCM,
 * not from tinycrypt (see ground/secunit.py). */
#include "keystore.h"
#include "unity.h"

#include <string.h>

static keystore_t ks;

static const uint8_t SPARE[KS_KEY_LEN] = {   /* lab-keys.json spare_keys[0] */
    0xCBu, 0x58u, 0x9Bu, 0xCEu, 0xDBu, 0x94u, 0x24u, 0x1Bu, 0x2Bu, 0x8Cu,
    0x25u, 0x3Fu, 0xCEu, 0x0Fu, 0x76u, 0x07u, 0xB1u, 0x34u, 0x41u, 0x8Fu,
    0xB7u, 0xE2u, 0x96u, 0xE2u, 0x1Bu, 0x1Bu, 0x2Au, 0x8Fu, 0xA6u, 0xFAu,
    0x64u, 0xA1u
};
static const uint8_t NONCE[KS_NONCE_LEN] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
/* AES-128-CCM(kek, NONCE, SPARE, aad "KEY\x02"), tag 16, from Python. */
static const uint8_t WRAPPED_SLOT2[KS_WRAPPED_LEN] = {
    0xC9u, 0xAAu, 0xCBu, 0x8Cu, 0xE7u, 0xB8u, 0x61u, 0xE6u, 0x93u, 0x3Fu,
    0x17u, 0x2Fu, 0xF8u, 0x7Eu, 0xD4u, 0x77u, 0xB5u, 0x56u, 0xBCu, 0x07u,
    0xF6u, 0xBBu, 0x81u, 0xF8u, 0xB2u, 0x41u, 0xFCu, 0x63u, 0x84u, 0xA7u,
    0x88u, 0x16u, 0x92u, 0xD6u, 0x63u, 0x0Eu, 0xD8u, 0x02u, 0xF2u, 0xF2u,
    0xB5u, 0x9Fu, 0x9Du, 0xA9u, 0x6Eu, 0x69u, 0x2Fu, 0x5Fu
};

void setUp(void) { ks_factory(&ks); }
void tearDown(void) { }

static void test_factory_state(void)
{
    uint32_t v;
    TEST_ASSERT_EQUAL_UINT8(1, ks.active);
    TEST_ASSERT_EQUAL_UINT8(KS_ACTIVE, ks.slot[1].state);
    TEST_ASSERT_EQUAL_UINT8(KS_EMPTY, ks.slot[2].state);
    TEST_ASSERT_EQUAL_INT(0, ks_counter(&ks, 1, &v));
    TEST_ASSERT_EQUAL_UINT32(0, v);
    TEST_ASSERT_EQUAL_INT(0, ks_challenge(&ks, &v));
    TEST_ASSERT_EQUAL_UINT32(1, v);
    TEST_ASSERT_EQUAL_HEX32(0xF0AD4481u, ks.slot[1].kcv[0]);   /* Python */
    TEST_ASSERT_TRUE(ks_counter_untrusted(&ks, 1));          /* first contact */
    TEST_ASSERT_FALSE(ks_counter_untrusted(&ks, 2));
    ks_counter_floor(&ks, 1, 77);
    TEST_ASSERT_FALSE(ks_counter_untrusted(&ks, 1));
    TEST_ASSERT_EQUAL_UINT32(77, ks.slot[1].ctr[2]);
}

static void test_mac_matches_ground(void)
{
    static const uint8_t prov_abc[16] = {
        0x58u, 0x1Bu, 0xC2u, 0x7Bu, 0x1Au, 0xB0u, 0xC2u, 0xD8u,
        0x3Eu, 0xE3u, 0x98u, 0x67u, 0x73u, 0x67u, 0xDCu, 0x71u };
    static const uint8_t rec_abc[16] = {
        0xE2u, 0x38u, 0x42u, 0x70u, 0x59u, 0xFFu, 0xAFu, 0x2Eu,
        0x89u, 0xA9u, 0x07u, 0xAAu, 0x45u, 0x4Au, 0x10u, 0x2Bu };
    uint8_t mac[16];
    TEST_ASSERT_EQUAL_INT(0, ks_mac(&ks, 1, (const uint8_t *)"abc", 3, mac));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(prov_abc, mac, 16);
    TEST_ASSERT_EQUAL_INT(0, ks_mac(&ks, 0, (const uint8_t *)"abc", 3, mac));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(rec_abc, mac, 16);
    TEST_ASSERT_EQUAL_INT(-1, ks_mac(&ks, 2, (const uint8_t *)"abc", 3, mac));
    TEST_ASSERT_EQUAL_INT(-1, ks_mac(&ks, 3, (const uint8_t *)"abc", 3, mac));
}

static void test_load_unwraps_ground_key(void)
{
    ks_counter_set(&ks, 2, 99);
    TEST_ASSERT_EQUAL_INT(0, ks_load(&ks, 2, NONCE, WRAPPED_SLOT2));
    TEST_ASSERT_EQUAL_UINT8(KS_LOADED, ks.slot[2].state);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SPARE, ks.slot[2].key, KS_KEY_LEN);
    TEST_ASSERT_EQUAL_HEX32(0xDB3517B9u, ks.slot[2].kcv[0]);   /* Python */
    TEST_ASSERT_EQUAL_UINT32(0, ks.slot[2].ctr[0]);           /* restarts */
    TEST_ASSERT_EQUAL_UINT8(1, ks.active);                    /* not active */
}

static void test_load_rejects_bad_wrap(void)
{
    uint8_t w[KS_WRAPPED_LEN];
    memcpy(w, WRAPPED_SLOT2, sizeof(w));
    w[40] ^= 0x01u;                                   /* tag bit */
    TEST_ASSERT_EQUAL_INT(-2, ks_load(&ks, 2, NONCE, w));
    TEST_ASSERT_EQUAL_UINT8(KS_EMPTY, ks.slot[2].state);
    w[40] ^= 0x01u; w[0] ^= 0x80u;                    /* ciphertext bit */
    TEST_ASSERT_EQUAL_INT(-2, ks_load(&ks, 2, NONCE, w));
    /* wrapped for slot 2, presented for slot 1: AAD binds the slot. */
    ks.slot[2].state = KS_EMPTY;
    TEST_ASSERT_EQUAL_INT(0, ks_load(&ks, 2, NONCE, WRAPPED_SLOT2));
    TEST_ASSERT_EQUAL_INT(0, ks_activate(&ks, 2));
    TEST_ASSERT_EQUAL_INT(-2, ks_load(&ks, 1, NONCE, WRAPPED_SLOT2));
    TEST_ASSERT_EQUAL_UINT8(KS_LOADED, ks.slot[1].state);    /* unchanged */
}

static void test_load_refuses_active_slot(void)
{
    TEST_ASSERT_EQUAL_INT(-1, ks_load(&ks, 1, NONCE, WRAPPED_SLOT2));
    TEST_ASSERT_EQUAL_INT(-1, ks_load(&ks, 0, NONCE, WRAPPED_SLOT2));
    TEST_ASSERT_EQUAL_INT(-1, ks_load(&ks, 3, NONCE, WRAPPED_SLOT2));
}

static void test_activate_and_fallback(void)
{
    TEST_ASSERT_EQUAL_INT(-1, ks_activate(&ks, 2));          /* empty */
    TEST_ASSERT_EQUAL_INT(0, ks_load(&ks, 2, NONCE, WRAPPED_SLOT2));
    TEST_ASSERT_EQUAL_INT(0, ks_activate(&ks, 2));
    TEST_ASSERT_EQUAL_UINT8(2, ks.active);
    TEST_ASSERT_EQUAL_UINT8(KS_ACTIVE, ks.slot[2].state);
    TEST_ASSERT_EQUAL_UINT8(KS_LOADED, ks.slot[1].state);    /* fallback */
    TEST_ASSERT_EQUAL_INT(-1, ks_activate(&ks, 2));          /* already */
    TEST_ASSERT_EQUAL_INT(0, ks_activate(&ks, 1));           /* back */
    TEST_ASSERT_EQUAL_UINT8(1, ks.active);
}

static void test_erase(void)
{
    size_t i; int all_zero = 1;
    TEST_ASSERT_EQUAL_INT(-1, ks_erase(&ks, 1));             /* active */
    TEST_ASSERT_EQUAL_INT(0, ks_load(&ks, 2, NONCE, WRAPPED_SLOT2));
    TEST_ASSERT_EQUAL_INT(0, ks_erase(&ks, 2));
    TEST_ASSERT_EQUAL_UINT8(KS_EMPTY, ks.slot[2].state);
    for (i = 0; i < KS_KEY_LEN; i++) {
        all_zero &= ks.slot[2].key[i] == 0u && ks.slot[2].key_copy[0][i] == 0u &&
                    ks.slot[2].key_copy[1][i] == 0u;
    }
    TEST_ASSERT_TRUE(all_zero);
    TEST_ASSERT_FALSE(ks_usable(&ks, 2));
}

static void test_counter_vote(void)
{
    uint32_t v;
    ks_counter_set(&ks, 1, 500);
    ks.slot[1].ctr[1] ^= 0x80000000u;                 /* one upset */
    TEST_ASSERT_EQUAL_INT(0, ks_counter(&ks, 1, &v));
    TEST_ASSERT_EQUAL_UINT32(500, v);
    TEST_ASSERT_EQUAL_UINT32(500, ks.slot[1].ctr[1]); /* repaired */
    ks.slot[1].ctr[0] = 501;                          /* the copy used first */
    TEST_ASSERT_EQUAL_INT(0, ks_counter(&ks, 1, &v));
    TEST_ASSERT_EQUAL_UINT32(500, v);                 /* out-voted by copies 1, 2 */
    TEST_ASSERT_EQUAL_UINT32(500, ks.slot[1].ctr[0]);
    /* No majority: the largest copy, never below the true value. */
    ks.slot[1].ctr[0] = 7; ks.slot[1].ctr[1] = 500; ks.slot[1].ctr[2] = 9000;
    TEST_ASSERT_EQUAL_INT(1, ks_counter(&ks, 1, &v));
    TEST_ASSERT_EQUAL_UINT32(9000, v);
    TEST_ASSERT_EQUAL_UINT32(9000, ks.slot[1].ctr[0]);
    ks.challenge[2] = 77;
    TEST_ASSERT_EQUAL_INT(0, ks_challenge(&ks, &v));
    TEST_ASSERT_EQUAL_UINT32(1, v);
    TEST_ASSERT_EQUAL_INT(-1, ks_counter(&ks, 0, &v));
}

static void test_scrub_repairs_single_upset(void)
{
    ks.slot[1].key[5] ^= 0x10u;                       /* copy 0, used for MACs */
    ks.slot[1].kcv[2] ^= 0x1u;
    TEST_ASSERT_EQUAL_UINT(0u, ks_scrub(&ks));
    TEST_ASSERT_EQUAL_UINT8(KS_ACTIVE, ks.slot[1].state);
    TEST_ASSERT_EQUAL_UINT32(1, ks.scrub_repairs);
    TEST_ASSERT_EQUAL_HEX8(0xC0u, ks.slot[1].key[5]);   /* provisioned value */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ks.slot[1].key_copy[0], ks.slot[1].key, KS_KEY_LEN);
    TEST_ASSERT_EQUAL_HEX32(0xF0AD4481u, ks.slot[1].kcv[2]);
    TEST_ASSERT_EQUAL_UINT(0u, ks_scrub(&ks));        /* clean now */
    TEST_ASSERT_EQUAL_UINT32(1, ks.scrub_repairs);
}

static void test_scrub_detects_lost_key(void)
{
    ks.slot[1].key[0] ^= 0x01u;                       /* same bit in two copies */
    ks.slot[1].key_copy[0][0] ^= 0x01u;
    TEST_ASSERT_EQUAL_UINT(1u << 1, ks_scrub(&ks));
    TEST_ASSERT_EQUAL_UINT8(KS_CORRUPT, ks.slot[1].state);
    TEST_ASSERT_FALSE(ks_usable(&ks, 1));
    TEST_ASSERT_EQUAL_UINT32(1, ks.scrub_errors);
    TEST_ASSERT_EQUAL_UINT(0u, ks_scrub(&ks));        /* reported once */
    TEST_ASSERT_TRUE(ks_usable(&ks, 0));              /* recovery key is ROM */
}

static void test_nv_image(void)
{
    uint8_t img[KS_IMAGE_LEN];
    keystore_t back;
    ks_counter_set(&ks, 1, 1234);
    TEST_ASSERT_EQUAL_size_t(0, ks_encode(&ks, img, sizeof(img) - 1u));
    TEST_ASSERT_EQUAL_size_t(KS_IMAGE_LEN, ks_encode(&ks, img, sizeof(img)));
    memset(&back, 0, sizeof(back));
    TEST_ASSERT_EQUAL_INT(0, ks_decode(&back, img, sizeof(img)));
    TEST_ASSERT_EQUAL_MEMORY(&ks, &back, sizeof(ks));
    img[20] ^= 1u;                                    /* CRC catches it */
    TEST_ASSERT_EQUAL_INT(-1, ks_decode(&back, img, sizeof(img)));
    memset(img, 0, sizeof(img));                      /* blank NV */
    TEST_ASSERT_EQUAL_INT(-1, ks_decode(&back, img, sizeof(img)));
    TEST_ASSERT_EQUAL_INT(-1, ks_decode(&back, img, 10));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_factory_state);
    RUN_TEST(test_mac_matches_ground);
    RUN_TEST(test_load_unwraps_ground_key);
    RUN_TEST(test_load_rejects_bad_wrap);
    RUN_TEST(test_load_refuses_active_slot);
    RUN_TEST(test_activate_and_fallback);
    RUN_TEST(test_erase);
    RUN_TEST(test_counter_vote);
    RUN_TEST(test_scrub_repairs_single_upset);
    RUN_TEST(test_scrub_detects_lost_key);
    RUN_TEST(test_nv_image);
    return UNITY_END();
}
