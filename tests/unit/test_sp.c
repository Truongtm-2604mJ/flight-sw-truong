/* Unit suite: CCSDS Space Packet encoder and decoder (sp.c).
 *
 * Ported to Unity from session-04/lab-4.1-space-packet/test_sp.c by tests/support/port_to_unity.py.
 */
/*
 * Lab 4.1 - Space Packet test suite (host build).
 *
 * Every expected value below is computed by hand in the comment next to
 * it, not produced by running the encoder. Build and run: make test
 */
#include "sp.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

static sp_primary_header_t tm_header(void)
{
    sp_primary_header_t h;
    h.version      = 0;
    h.type         = SP_TM;
    h.sec_hdr_flag = 1;
    h.apid         = 0x123;
    h.seq_flags    = SP_SEQ_FLAGS_UNSEG;
    h.seq_count    = 0x0042;
    h.data_length  = 0xFFFF; /* deliberately wrong: encode must ignore it */
    return h;
}

/* Required test 1: known good TM packet, octet by octet. */
static void test_known_good_tm_packet(void)
{
    /*
     * version 000, type 0 (TM), sec hdr 1, APID 0x123 = 001 0010 0011
     *   octet 0 = 000 0 1 001 = 0x09      octet 1 = 0010 0011 = 0x23
     * seq flags 11, seq count 0x0042 = 00 0000 0100 0010
     *   octet 2 = 11 000000  = 0xC0       octet 3 = 0100 0010 = 0x42
     * payload 4 octets -> length field = 4 - 1 = 3
     *   octet 4 = 0x00                    octet 5 = 0x03
     */
    static const uint8_t payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    static const uint8_t expected[10] = {
        0x09, 0x23, 0xC0, 0x42, 0x00, 0x03, 0xDE, 0xAD, 0xBE, 0xEF
    };
    uint8_t out[16];
    sp_primary_header_t h = tm_header();
    int n;

    memset(out, 0xAA, sizeof(out));
    n = sp_encode(&h, payload, sizeof(payload), out, sizeof(out));

    TEST_ASSERT_EQUAL_INT64((int64_t)(10), (int64_t)(n));
    TEST_ASSERT_EQUAL_MEMORY((expected), (out), (sizeof(expected)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0xAA), (int64_t)(out[10])); /* nothing written past the packet */
}

/* A TC with different field values, so a swapped field cannot hide. */
static void test_known_good_tc_packet(void)
{
    /*
     * version 000, type 1 (TC), sec hdr 0, APID 0x7FE = 111 1111 1110
     *   octet 0 = 000 1 0 111 = 0x17      octet 1 = 0xFE
     * seq flags 01, seq count 0x3FFF
     *   octet 2 = 01 111111  = 0x7F       octet 3 = 0xFF
     * payload 1 octet -> length field = 0
     */
    static const uint8_t payload[1] = { 0x5A };
    static const uint8_t expected[7] = {
        0x17, 0xFE, 0x7F, 0xFF, 0x00, 0x00, 0x5A
    };
    uint8_t out[7];
    sp_primary_header_t h;
    int n;

    h.version = 0; h.type = SP_TC; h.sec_hdr_flag = 0; h.apid = 0x7FE;
    h.seq_flags = 1; h.seq_count = 0x3FFF; h.data_length = 0;

    n = sp_encode(&h, payload, sizeof(payload), out, sizeof(out));
    TEST_ASSERT_EQUAL_INT64((int64_t)(7), (int64_t)(n));
    TEST_ASSERT_EQUAL_MEMORY((expected), (out), (sizeof(expected)));
}

/* Required test 2: length field is N - 1, decode returns N. */
static void test_length_field_round_trip(void)
{
    static const size_t sizes[] = { 1, 2, 10, 255, 256, 257, 1000 };
    static uint8_t payload[1000];
    static uint8_t out[SP_PRIMARY_HEADER_LEN + 1000];
    size_t i;

    for (i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 7u + 1u);
    }

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        size_t n = sizes[i];
        sp_primary_header_t h = tm_header();
        sp_primary_header_t d;
        const uint8_t *p = NULL;
        int rc;

        rc = sp_encode(&h, payload, n, out, sizeof(out));
        TEST_ASSERT_EQUAL_INT64((int64_t)((long)(SP_PRIMARY_HEADER_LEN + n)), (int64_t)(rc));

        /* The encoded field itself, read straight from the octets. */
        TEST_ASSERT_EQUAL_INT64((int64_t)(n - 1), (int64_t)(((unsigned)out[4] << 8) | out[5]));

        rc = sp_decode(out, SP_PRIMARY_HEADER_LEN + n, &d, &p);
        TEST_ASSERT_EQUAL_INT64((int64_t)((long)n), (int64_t)(rc));
        TEST_ASSERT_EQUAL_INT64((int64_t)(n - 1), (int64_t)(d.data_length));
        TEST_ASSERT_TRUE(p == &out[SP_PRIMARY_HEADER_LEN]);
        TEST_ASSERT_EQUAL_MEMORY((payload), (p), (n));

        /* All other fields survive the round trip. */
        TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(d.version));
        TEST_ASSERT_EQUAL_INT64((int64_t)(SP_TM), (int64_t)(d.type));
        TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(d.sec_hdr_flag));
        TEST_ASSERT_EQUAL_INT64((int64_t)(0x123), (int64_t)(d.apid));
        TEST_ASSERT_EQUAL_INT64((int64_t)(3), (int64_t)(d.seq_flags));
        TEST_ASSERT_EQUAL_INT64((int64_t)(0x0042), (int64_t)(d.seq_count));
    }
}

/* The example quoted in the session notes: 10 octets encodes as 9. */
static void test_length_ten_octets_encodes_as_nine(void)
{
    uint8_t payload[10] = { 0 };
    uint8_t out[16];
    sp_primary_header_t h = tm_header();

    TEST_ASSERT_EQUAL_INT64((int64_t)(16), (int64_t)(sp_encode(&h, payload, 10, out, sizeof(out))));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0x00), (int64_t)(out[4]));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0x09), (int64_t)(out[5]));
}

/* Both ends of the 16 bit length field. */
static void test_length_limits(void)
{
    static uint8_t payload[SP_PAYLOAD_MAX + 1u];
    static uint8_t out[SP_PRIMARY_HEADER_LEN + SP_PAYLOAD_MAX + 1u];
    sp_primary_header_t h = tm_header();
    sp_primary_header_t d;
    const uint8_t *p;

    /* Empty data field is not a legal Space Packet. */
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_PAYLOAD), (int64_t)(sp_encode(&h, payload, 0, out, sizeof(out))));

    /* Largest packet: 65536 octets encodes as 0xFFFF. */
    TEST_ASSERT_EQUAL_INT64((int64_t)((long)(SP_PRIMARY_HEADER_LEN + SP_PAYLOAD_MAX)), (int64_t)(sp_encode(&h, payload, SP_PAYLOAD_MAX, out, sizeof(out))));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0xFF), (int64_t)(out[4]));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0xFF), (int64_t)(out[5]));
    TEST_ASSERT_EQUAL_INT64((int64_t)((long)SP_PAYLOAD_MAX), (int64_t)(sp_decode(out, SP_PRIMARY_HEADER_LEN + SP_PAYLOAD_MAX, &d, &p)));

    /* One more octet cannot be represented. */
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_PAYLOAD), (int64_t)(sp_encode(&h, payload, SP_PAYLOAD_MAX + 1u, out, sizeof(out))));
}

/* Required test 3: APID wider than 11 bits is rejected, not truncated. */
static void test_apid_out_of_range_rejected(void)
{
    static const uint8_t payload[2] = { 1, 2 };
    uint8_t out[8];
    sp_primary_header_t h = tm_header();

    memset(out, 0xAA, sizeof(out));

    h.apid = 0x800; /* first value that needs 12 bits */
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_FIELD), (int64_t)(sp_encode(&h, payload, 2, out, sizeof(out))));
    h.apid = 0xFFFF;
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_FIELD), (int64_t)(sp_encode(&h, payload, 2, out, sizeof(out))));

    /* A rejected call must not leave a half written packet behind. */
    TEST_ASSERT_EQUAL_INT64((int64_t)(0xAA), (int64_t)(out[0]));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0xAA), (int64_t)(out[1]));

    /* The largest legal APID (the idle APID) is still accepted. */
    h.apid = SP_APID_MAX;
    TEST_ASSERT_EQUAL_INT64((int64_t)(8), (int64_t)(sp_encode(&h, payload, 2, out, sizeof(out))));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0x0F), (int64_t)(out[0])); /* 000 0 1 111 */
    TEST_ASSERT_EQUAL_INT64((int64_t)(0xFF), (int64_t)(out[1]));
}

/* Same rule for every other field. */
static void test_other_fields_out_of_range_rejected(void)
{
    static const uint8_t payload[1] = { 0 };
    uint8_t out[8];
    sp_primary_header_t h;

    h = tm_header(); h.version = 1;
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_FIELD), (int64_t)(sp_encode(&h, payload, 1, out, sizeof(out))));
    h = tm_header(); h.type = (sp_type_t)2;
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_FIELD), (int64_t)(sp_encode(&h, payload, 1, out, sizeof(out))));
    h = tm_header(); h.sec_hdr_flag = 2;
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_FIELD), (int64_t)(sp_encode(&h, payload, 1, out, sizeof(out))));
    h = tm_header(); h.seq_flags = 4;
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_FIELD), (int64_t)(sp_encode(&h, payload, 1, out, sizeof(out))));
    h = tm_header(); h.seq_count = 0x4000;
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_FIELD), (int64_t)(sp_encode(&h, payload, 1, out, sizeof(out))));
}

static void test_encode_capacity_and_null(void)
{
    static const uint8_t payload[4] = { 1, 2, 3, 4 };
    uint8_t out[10];
    sp_primary_header_t h = tm_header();

    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_CAPACITY), (int64_t)(sp_encode(&h, payload, 4, out, 9)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(10), (int64_t)(sp_encode(&h, payload, 4, out, 10)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_ARG), (int64_t)(sp_encode(NULL, payload, 4, out, 10)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_ARG), (int64_t)(sp_encode(&h, NULL, 4, out, 10)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_ARG), (int64_t)(sp_encode(&h, payload, 4, NULL, 10)));
}

/* Required test 4: input shorter than six octets fails cleanly. */
static void test_decode_truncated_input(void)
{
    static const uint8_t pkt[10] = {
        0x09, 0x23, 0xC0, 0x42, 0x00, 0x03, 0xDE, 0xAD, 0xBE, 0xEF
    };
    sp_primary_header_t d;
    const uint8_t *p = (const uint8_t *)&d; /* sentinel */
    size_t len;

    for (len = 0; len < SP_PRIMARY_HEADER_LEN; len++) {
        TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_TRUNCATED), (int64_t)(sp_decode(pkt, len, &d, &p)));
    }
    TEST_ASSERT_TRUE(p == (const uint8_t *)&d); /* output pointer untouched */

    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_ARG), (int64_t)(sp_decode(NULL, 10, &d, &p)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_ARG), (int64_t)(sp_decode(pkt, 10, NULL, &p)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_ARG), (int64_t)(sp_decode(pkt, 10, &d, NULL)));
}

/* Required test 5: header claims more payload than the buffer holds. */
static void test_decode_inconsistent_length(void)
{
    /* Length field 3 -> 4 payload octets -> 10 octets in total. */
    static const uint8_t pkt[10] = {
        0x09, 0x23, 0xC0, 0x42, 0x00, 0x03, 0xDE, 0xAD, 0xBE, 0xEF
    };
    /* Header only, claiming the maximum payload. */
    static const uint8_t liar[6] = { 0x09, 0x23, 0xC0, 0x42, 0xFF, 0xFF };
    sp_primary_header_t d;
    const uint8_t *p = NULL;

    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_LENGTH), (int64_t)(sp_decode(pkt, 9, &d, &p))); /* one octet short */
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_LENGTH), (int64_t)(sp_decode(pkt, 6, &d, &p))); /* header only     */
    TEST_ASSERT_TRUE(p == NULL);                                   /* never exposed   */
    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_LENGTH), (int64_t)(sp_decode(liar, sizeof(liar), &d, &p)));

    /* Exact size is accepted. */
    TEST_ASSERT_EQUAL_INT64((int64_t)(4), (int64_t)(sp_decode(pkt, 10, &d, &p)));

    /* A longer buffer (packet stream) is accepted and reports only the
     * first packet, so the caller can step to the next one. */
    {
        uint8_t stream[14];
        memcpy(stream, pkt, 10);
        memset(&stream[10], 0x55, 4);
        TEST_ASSERT_EQUAL_INT64((int64_t)(4), (int64_t)(sp_decode(stream, sizeof(stream), &d, &p)));
    }
}

static void test_decode_rejects_unknown_version(void)
{
    /* Same packet as above with version bits 001. */
    static const uint8_t pkt[10] = {
        0x29, 0x23, 0xC0, 0x42, 0x00, 0x03, 0xDE, 0xAD, 0xBE, 0xEF
    };
    sp_primary_header_t d;
    const uint8_t *p;

    TEST_ASSERT_EQUAL_INT64((int64_t)(SP_ERR_VERSION), (int64_t)(sp_decode(pkt, sizeof(pkt), &d, &p)));
}

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_known_good_tm_packet);
    RUN_TEST(test_known_good_tc_packet);
    RUN_TEST(test_length_field_round_trip);
    RUN_TEST(test_length_ten_octets_encodes_as_nine);
    RUN_TEST(test_length_limits);
    RUN_TEST(test_apid_out_of_range_rejected);
    RUN_TEST(test_other_fields_out_of_range_rejected);
    RUN_TEST(test_encode_capacity_and_null);
    RUN_TEST(test_decode_truncated_input);
    RUN_TEST(test_decode_inconsistent_length);
    RUN_TEST(test_decode_rejects_unknown_version);
    return UNITY_END();
}
