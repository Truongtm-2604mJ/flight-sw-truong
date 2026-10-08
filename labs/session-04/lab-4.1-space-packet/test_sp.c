/*
 * Lab 4.1 - Space Packet test suite (host build).
 *
 * Every expected value below is computed by hand in the comment next to
 * it, not produced by running the encoder. Build and run: make test
 */
#include "sp.h"
#include "test_util.h"

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

    CHECK_EQ(n, 10);
    CHECK_MEM(out, expected, sizeof(expected));
    CHECK_EQ(out[10], 0xAA); /* nothing written past the packet */
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
    CHECK_EQ(n, 7);
    CHECK_MEM(out, expected, sizeof(expected));
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
        CHECK_EQ(rc, (long)(SP_PRIMARY_HEADER_LEN + n));

        /* The encoded field itself, read straight from the octets. */
        CHECK_EQ(((unsigned)out[4] << 8) | out[5], n - 1);

        rc = sp_decode(out, SP_PRIMARY_HEADER_LEN + n, &d, &p);
        CHECK_EQ(rc, (long)n);
        CHECK_EQ(d.data_length, n - 1);
        CHECK(p == &out[SP_PRIMARY_HEADER_LEN]);
        CHECK_MEM(p, payload, n);

        /* All other fields survive the round trip. */
        CHECK_EQ(d.version, 0);
        CHECK_EQ(d.type, SP_TM);
        CHECK_EQ(d.sec_hdr_flag, 1);
        CHECK_EQ(d.apid, 0x123);
        CHECK_EQ(d.seq_flags, 3);
        CHECK_EQ(d.seq_count, 0x0042);
    }
}

/* The example quoted in the session notes: 10 octets encodes as 9. */
static void test_length_ten_octets_encodes_as_nine(void)
{
    uint8_t payload[10] = { 0 };
    uint8_t out[16];
    sp_primary_header_t h = tm_header();

    CHECK_EQ(sp_encode(&h, payload, 10, out, sizeof(out)), 16);
    CHECK_EQ(out[4], 0x00);
    CHECK_EQ(out[5], 0x09);
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
    CHECK_EQ(sp_encode(&h, payload, 0, out, sizeof(out)), SP_ERR_PAYLOAD);

    /* Largest packet: 65536 octets encodes as 0xFFFF. */
    CHECK_EQ(sp_encode(&h, payload, SP_PAYLOAD_MAX, out, sizeof(out)),
             (long)(SP_PRIMARY_HEADER_LEN + SP_PAYLOAD_MAX));
    CHECK_EQ(out[4], 0xFF);
    CHECK_EQ(out[5], 0xFF);
    CHECK_EQ(sp_decode(out, SP_PRIMARY_HEADER_LEN + SP_PAYLOAD_MAX, &d, &p),
             (long)SP_PAYLOAD_MAX);

    /* One more octet cannot be represented. */
    CHECK_EQ(sp_encode(&h, payload, SP_PAYLOAD_MAX + 1u, out, sizeof(out)),
             SP_ERR_PAYLOAD);
}

/* Required test 3: APID wider than 11 bits is rejected, not truncated. */
static void test_apid_out_of_range_rejected(void)
{
    static const uint8_t payload[2] = { 1, 2 };
    uint8_t out[8];
    sp_primary_header_t h = tm_header();

    memset(out, 0xAA, sizeof(out));

    h.apid = 0x800; /* first value that needs 12 bits */
    CHECK_EQ(sp_encode(&h, payload, 2, out, sizeof(out)), SP_ERR_FIELD);
    h.apid = 0xFFFF;
    CHECK_EQ(sp_encode(&h, payload, 2, out, sizeof(out)), SP_ERR_FIELD);

    /* A rejected call must not leave a half written packet behind. */
    CHECK_EQ(out[0], 0xAA);
    CHECK_EQ(out[1], 0xAA);

    /* The largest legal APID (the idle APID) is still accepted. */
    h.apid = SP_APID_MAX;
    CHECK_EQ(sp_encode(&h, payload, 2, out, sizeof(out)), 8);
    CHECK_EQ(out[0], 0x0F); /* 000 0 1 111 */
    CHECK_EQ(out[1], 0xFF);
}

/* Same rule for every other field. */
static void test_other_fields_out_of_range_rejected(void)
{
    static const uint8_t payload[1] = { 0 };
    uint8_t out[8];
    sp_primary_header_t h;

    h = tm_header(); h.version = 1;
    CHECK_EQ(sp_encode(&h, payload, 1, out, sizeof(out)), SP_ERR_FIELD);
    h = tm_header(); h.type = (sp_type_t)2;
    CHECK_EQ(sp_encode(&h, payload, 1, out, sizeof(out)), SP_ERR_FIELD);
    h = tm_header(); h.sec_hdr_flag = 2;
    CHECK_EQ(sp_encode(&h, payload, 1, out, sizeof(out)), SP_ERR_FIELD);
    h = tm_header(); h.seq_flags = 4;
    CHECK_EQ(sp_encode(&h, payload, 1, out, sizeof(out)), SP_ERR_FIELD);
    h = tm_header(); h.seq_count = 0x4000;
    CHECK_EQ(sp_encode(&h, payload, 1, out, sizeof(out)), SP_ERR_FIELD);
}

static void test_encode_capacity_and_null(void)
{
    static const uint8_t payload[4] = { 1, 2, 3, 4 };
    uint8_t out[10];
    sp_primary_header_t h = tm_header();

    CHECK_EQ(sp_encode(&h, payload, 4, out, 9), SP_ERR_CAPACITY);
    CHECK_EQ(sp_encode(&h, payload, 4, out, 10), 10);
    CHECK_EQ(sp_encode(NULL, payload, 4, out, 10), SP_ERR_ARG);
    CHECK_EQ(sp_encode(&h, NULL, 4, out, 10), SP_ERR_ARG);
    CHECK_EQ(sp_encode(&h, payload, 4, NULL, 10), SP_ERR_ARG);
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
        CHECK_EQ(sp_decode(pkt, len, &d, &p), SP_ERR_TRUNCATED);
    }
    CHECK(p == (const uint8_t *)&d); /* output pointer untouched */

    CHECK_EQ(sp_decode(NULL, 10, &d, &p), SP_ERR_ARG);
    CHECK_EQ(sp_decode(pkt, 10, NULL, &p), SP_ERR_ARG);
    CHECK_EQ(sp_decode(pkt, 10, &d, NULL), SP_ERR_ARG);
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

    CHECK_EQ(sp_decode(pkt, 9, &d, &p), SP_ERR_LENGTH); /* one octet short */
    CHECK_EQ(sp_decode(pkt, 6, &d, &p), SP_ERR_LENGTH); /* header only     */
    CHECK(p == NULL);                                   /* never exposed   */
    CHECK_EQ(sp_decode(liar, sizeof(liar), &d, &p), SP_ERR_LENGTH);

    /* Exact size is accepted. */
    CHECK_EQ(sp_decode(pkt, 10, &d, &p), 4);

    /* A longer buffer (packet stream) is accepted and reports only the
     * first packet, so the caller can step to the next one. */
    {
        uint8_t stream[14];
        memcpy(stream, pkt, 10);
        memset(&stream[10], 0x55, 4);
        CHECK_EQ(sp_decode(stream, sizeof(stream), &d, &p), 4);
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

    CHECK_EQ(sp_decode(pkt, sizeof(pkt), &d, &p), SP_ERR_VERSION);
}

int main(void)
{
    printf("Lab 4.1 - Space Packet encoder/decoder tests\n\n");
    RUN(test_known_good_tm_packet);
    RUN(test_known_good_tc_packet);
    RUN(test_length_field_round_trip);
    RUN(test_length_ten_octets_encodes_as_nine);
    RUN(test_length_limits);
    RUN(test_apid_out_of_range_rejected);
    RUN(test_other_fields_out_of_range_rejected);
    RUN(test_encode_capacity_and_null);
    RUN(test_decode_truncated_input);
    RUN(test_decode_inconsistent_length);
    RUN(test_decode_rejects_unknown_version);
    return TEST_SUMMARY();
}
