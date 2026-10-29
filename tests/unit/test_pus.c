/* Unit suite: PUS packet layer, CRC and telecommand handler (pus.c,
 * crc16.c, tc_handler.c).
 *
 * Ported to Unity from session-04/lab-4.2-pus-service17/tests/test_pus.c by tests/support/port_to_unity.py.
 */
/*
 * Lab 4.2 - host tests for the CRC, the PUS packet layer and the
 * telecommand handler. No RTEMS needed. Build and run: make test
 *
 * Reference CRC values were produced with an independent implementation
 * (Python binascii.crc_hqx, initial value 0xFFFF), not with crc16.c.
 */
#include "crc16.h"
#include "pus.h"
#include "tc_handler.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

#define APID_CMD 0x010u

/* ---- Capture sink: records every TM the handler emits ---------------- */

#define CAP_MAX 16
static uint8_t cap_pkt[CAP_MAX][PUS_MAX_PACKET];
static size_t  cap_len[CAP_MAX];
static int     cap_count;
static int     cap_fail_from = -1; /* sink starts failing at this index */

static int capture_sink(const uint8_t *packet, size_t len, void *arg)
{
    (void)arg;
    if (cap_fail_from >= 0 && cap_count >= cap_fail_from) {
        return 1;
    }
    if (cap_count < CAP_MAX) {
        memcpy(cap_pkt[cap_count], packet, len);
        cap_len[cap_count] = len;
    }
    cap_count++;
    return 0;
}

static obt_t fixed_now(void)
{
    obt_t t = { 2u, 0x8000u }; /* 2.5 s */
    return t;
}

static tc_ctx_t ctx;

static void setup(void)
{
    cap_count = 0;
    cap_fail_from = -1;
    memset(cap_pkt, 0, sizeof(cap_pkt));
    tc_init(&ctx, APID_CMD, capture_sink, NULL, fixed_now);
}

/* Field accessors on a captured TM packet. */
static unsigned tm_apid(int i)    { return ((cap_pkt[i][0] & 0x07u) << 8) | cap_pkt[i][1]; }
static unsigned tm_seq(int i)     { return ((cap_pkt[i][2] & 0x3Fu) << 8) | cap_pkt[i][3]; }
static unsigned tm_service(int i) { return cap_pkt[i][7]; }
static unsigned tm_subtype(int i) { return cap_pkt[i][8]; }
static unsigned tm_dest(int i)    { return ((unsigned)cap_pkt[i][11] << 8) | cap_pkt[i][12]; }
static const uint8_t *tm_data(int i) { return &cap_pkt[i][SP_PRIMARY_HEADER_LEN + PUS_TM_SEC_HDR_LEN]; }
static size_t tm_data_len(int i)  { return cap_len[i] - PUS_TM_MIN_LEN; }

static int tm_crc_ok(int i)
{
    size_t n = cap_len[i];
    return crc16_ccitt(cap_pkt[i], n - 2u) == pus_get_u16(&cap_pkt[i][n - 2u]);
}

/* The reference telecommand: TC(17,1), APID 0x010, sequence count 0,
 * acknowledge acceptance + completion, source ID 1.
 *
 *   18 10   000 1 1 000 0001 0000   version 0, TC, sec hdr, APID 0x010
 *   C0 00   11 00000000000000       unsegmented, sequence count 0
 *   00 06   data field 7 octets (5 header + 0 data + 2 CRC), minus 1
 *   29      PUS version 2, ack flags 1001
 *   11 01   service 17, subtype 1
 *   00 01   source ID 1
 *   30 6A   CRC-16 over the 11 octets above
 */
static const uint8_t TC_PING[13] = {
    0x18, 0x10, 0xC0, 0x00, 0x00, 0x06,
    0x29, 0x11, 0x01, 0x00, 0x01,
    0x30, 0x6A
};

/* ---- CRC -------------------------------------------------------------- */

static void test_crc_check_value(void)
{
    TEST_ASSERT_EQUAL_INT64((int64_t)(0x29B1), (int64_t)(crc16_ccitt((const uint8_t *)"123456789", 9)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0xFFFF), (int64_t)(crc16_ccitt((const uint8_t *)"", 0)));
    /* A packet followed by its own CRC has residue 0. */
    TEST_ASSERT_EQUAL_INT64((int64_t)(0x0000), (int64_t)(crc16_ccitt(TC_PING, sizeof(TC_PING))));
}

/* ---- Packet builders --------------------------------------------------- */

static void test_tc_build_matches_hand_computed_octets(void)
{
    uint8_t out[PUS_MAX_PACKET];
    int n = pus_tc_build(APID_CMD, 0, PUS_ACK_ACCEPTANCE | PUS_ACK_COMPLETION,
                         17, 1, 0x0001, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT64((int64_t)(13), (int64_t)(n));
    TEST_ASSERT_EQUAL_MEMORY((TC_PING), (out), (sizeof(TC_PING)));
}

static void test_tm_build_matches_hand_computed_octets(void)
{
    /*
     * TM(17,2), APID 0x010, sequence count 1, destination 1, time 2.5 s
     *   08 10   000 0 1 000 0001 0000   version 0, TM, sec hdr, APID 0x010
     *   C0 01   unsegmented, sequence count 1
     *   00 0E   data field 15 octets (13 header + 0 data + 2 CRC), minus 1
     *   20      PUS version 2, time reference status 0
     *   11 02   service 17, subtype 2
     *   00 00   message type counter (unused)
     *   00 01   destination ID 1
     *   00 00 00 02  80 00   time: 2 s + 0x8000/65536 s
     *   65 E3   CRC-16
     */
    static const uint8_t expected[21] = {
        0x08, 0x10, 0xC0, 0x01, 0x00, 0x0E,
        0x20, 0x11, 0x02, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x02, 0x80, 0x00,
        0x65, 0xE3
    };
    tm_source_t src = { APID_CMD, 1 };
    uint8_t out[PUS_MAX_PACKET];
    int n = pus_tm_build(&src, 17, 2, 1, fixed_now(), NULL, 0,
                         out, sizeof(out));
    TEST_ASSERT_EQUAL_INT64((int64_t)(21), (int64_t)(n));
    TEST_ASSERT_EQUAL_MEMORY((expected), (out), (sizeof(expected)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(src.seq_count)); /* consumed */
}

static void test_tm_sequence_count_wraps_at_14_bits(void)
{
    tm_source_t src = { APID_CMD, 0x3FFF };
    uint8_t out[PUS_MAX_PACKET];

    TEST_ASSERT_TRUE(pus_tm_build(&src, 17, 2, 0, fixed_now(), NULL, 0, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_INT64((int64_t)(0x3FFF), (int64_t)(((out[2] & 0x3Fu) << 8) | out[3]));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(src.seq_count));
}

static void test_tm_build_rejects_oversize_and_keeps_counter(void)
{
    static const uint8_t data[PUS_TM_MAX_DATA + 1u] = { 0 };
    tm_source_t src = { APID_CMD, 5 };
    uint8_t out[PUS_MAX_PACKET + 8u];

    TEST_ASSERT_TRUE(pus_tm_build(&src, 3, 25, 0, fixed_now(), data, sizeof(data),
                       out, sizeof(out)) < 0);
    TEST_ASSERT_EQUAL_INT64((int64_t)(5), (int64_t)(src.seq_count)); /* not consumed by a failed build */
    TEST_ASSERT_EQUAL_INT64((int64_t)((long)PUS_MAX_PACKET), (int64_t)(pus_tm_build(&src, 3, 25, 0, fixed_now(), data, PUS_TM_MAX_DATA,
                          out, sizeof(out))));
}

static void test_obt_conversion(void)
{
    obt_t t = obt_from_ns(2500000000ull);
    TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(t.coarse));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0x8000), (int64_t)(t.fine));
    t = obt_from_ns(999999999ull);
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(t.coarse));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0xFFFF), (int64_t)(t.fine));
}

/* ---- Acceptance criterion 1: valid command ----------------------------- */

static void test_valid_ping_gives_accept_report_complete_in_order(void)
{
    setup();
    tc_handle(&ctx, TC_PING, sizeof(TC_PING));

    TEST_ASSERT_EQUAL_INT64((int64_t)(3), (int64_t)(cap_count));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(tm_service(0)));  TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(tm_subtype(0))); /* acceptance */
    TEST_ASSERT_EQUAL_INT64((int64_t)(17), (int64_t)(tm_service(1))); TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(tm_subtype(1))); /* test report */
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(tm_service(2)));  TEST_ASSERT_EQUAL_INT64((int64_t)(7), (int64_t)(tm_subtype(2))); /* completion */

    /* Verification reports carry the request ID = first 4 TC octets. */
    TEST_ASSERT_EQUAL_INT64((int64_t)(4), (int64_t)(tm_data_len(0)));
    TEST_ASSERT_EQUAL_MEMORY((TC_PING), (tm_data(0)), (4));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(tm_data_len(1)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(4), (int64_t)(tm_data_len(2)));
    TEST_ASSERT_EQUAL_MEMORY((TC_PING), (tm_data(2)), (4));

    /* Reports go back to the commanding source. */
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(tm_dest(0))); TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(tm_dest(1))); TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(tm_dest(2)));

    TEST_ASSERT_TRUE(tm_crc_ok(0)); TEST_ASSERT_TRUE(tm_crc_ok(1)); TEST_ASSERT_TRUE(tm_crc_ok(2));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(ctx.accepted));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(ctx.rejected));
}

static void test_ack_flags_select_success_reports(void)
{
    uint8_t tc[PUS_MAX_PACKET];
    int n;

    /* No flags: the command still runs, only the test report comes back. */
    setup();
    n = pus_tc_build(APID_CMD, 1, 0, 17, 1, 1, NULL, 0, tc, sizeof(tc));
    tc_handle(&ctx, tc, (size_t)n);
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(cap_count));
    TEST_ASSERT_EQUAL_INT64((int64_t)(17), (int64_t)(tm_service(0)));

    /* All flags: acceptance, start, report, completion. No progress
     * report because the command has a single step. */
    setup();
    n = pus_tc_build(APID_CMD, 2, 0x0F, 17, 1, 1, NULL, 0, tc, sizeof(tc));
    tc_handle(&ctx, tc, (size_t)n);
    TEST_ASSERT_EQUAL_INT64((int64_t)(4), (int64_t)(cap_count));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(tm_subtype(0)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(3), (int64_t)(tm_subtype(1)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(17), (int64_t)(tm_service(2)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(7), (int64_t)(tm_subtype(3)));
}

/* ---- Acceptance criterion 2: corrupted checksum ------------------------ */

static void test_corrupted_checksum_is_rejected_and_not_executed(void)
{
    uint8_t bad[sizeof(TC_PING)];
    size_t bit;

    /* Flip the last CRC bit. */
    setup();
    memcpy(bad, TC_PING, sizeof(bad));
    bad[12] ^= 0x01u;
    tc_handle(&ctx, bad, sizeof(bad));

    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(cap_count));                     /* nothing else: no TM(17,2) */
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(tm_service(0)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(tm_subtype(0)));                 /* acceptance failure */
    TEST_ASSERT_EQUAL_INT64((int64_t)(5), (int64_t)(tm_data_len(0)));
    TEST_ASSERT_EQUAL_MEMORY((TC_PING), (tm_data(0)), (4));
    TEST_ASSERT_EQUAL_INT64((int64_t)(PUS_FAIL_CHECKSUM), (int64_t)(tm_data(0)[4]));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(tm_dest(0)));                    /* source ID not trusted */
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(ctx.accepted));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(ctx.rejected));

    /* Any single bit error in the PUS header or CRC must be caught and
     * must never produce a test report. (Bits in the primary header are
     * covered too, but can legitimately fail an earlier check.) */
    for (bit = 0; bit < sizeof(TC_PING) * 8u; bit++) {
        int i;
        setup();
        memcpy(bad, TC_PING, sizeof(bad));
        bad[bit / 8u] ^= (uint8_t)(0x80u >> (bit % 8u));
        tc_handle(&ctx, bad, sizeof(bad));
        TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(cap_count));
        TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(tm_subtype(0)));
        for (i = 0; i < cap_count && i < CAP_MAX; i++) {
            TEST_ASSERT_TRUE(tm_service(i) != 17);
        }
        if (bit >= 6u * 8u) {
            TEST_ASSERT_EQUAL_INT64((int64_t)(PUS_FAIL_CHECKSUM), (int64_t)(tm_data(0)[4]));
        }
    }
}

/* ---- Acceptance criterion 3: unimplemented service --------------------- */

static void test_unimplemented_service_has_its_own_failure_code(void)
{
    uint8_t tc[PUS_MAX_PACKET];
    int n;

    setup();
    n = pus_tc_build(APID_CMD, 7, 0x9, 8, 1, 1, NULL, 0, tc, sizeof(tc));
    tc_handle(&ctx, tc, (size_t)n);

    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(cap_count));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(tm_service(0)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(tm_subtype(0)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(PUS_FAIL_SERVICE), (int64_t)(tm_data(0)[4]));
    TEST_ASSERT_TRUE(PUS_FAIL_SERVICE != PUS_FAIL_CHECKSUM);
    TEST_ASSERT_EQUAL_MEMORY((tc), (tm_data(0)), (4));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(tm_dest(0))); /* packet was intact, so the source is known */
}

/* ---- Every other check in the validation sequence ---------------------- */

static void expect_single_failure(const uint8_t *frame, size_t len,
                                  pus_fail_t code)
{
    setup();
    tc_handle(&ctx, frame, len);
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(cap_count));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(tm_service(0)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(tm_subtype(0)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(code), (int64_t)(tm_data(0)[4]));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(ctx.accepted));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(ctx.rejected));
}

/* Recompute the CRC of a hand modified packet. */
static void fix_crc(uint8_t *pkt, size_t len)
{
    pus_put_u16(&pkt[len - 2u], crc16_ccitt(pkt, len - 2u));
}

static void test_validation_sequence_failure_codes(void)
{
    uint8_t tc[PUS_MAX_PACKET + 8u];
    static const uint8_t one = 0x00;
    int n;
    size_t len;

    /* Check 1: shorter than a primary header: counted, no report. */
    for (len = 0; len < 6u; len++) {
        setup();
        tc_handle(&ctx, TC_PING, len);
        TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(cap_count));
        TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(ctx.rejected));
    }

    /* Check 2: packet version number. */
    memcpy(tc, TC_PING, sizeof(TC_PING));
    tc[0] |= 0x20u;
    fix_crc(tc, sizeof(TC_PING));
    expect_single_failure(tc, sizeof(TC_PING), PUS_FAIL_SP_VERSION);

    /* Check 3: length. Frame shorter than the header claims... */
    expect_single_failure(TC_PING, sizeof(TC_PING) - 1u, PUS_FAIL_LENGTH);
    /* ...frame longer than the header claims... */
    memcpy(tc, TC_PING, sizeof(TC_PING));
    tc[sizeof(TC_PING)] = 0x00;
    expect_single_failure(tc, sizeof(TC_PING) + 1u, PUS_FAIL_LENGTH);
    /* ...consistent, but too short to hold a PUS header and CRC... */
    {
        static const uint8_t tiny[8] = { 0x18, 0x10, 0xC0, 0x00, 0x00, 0x01, 0xAB, 0xCD };
        expect_single_failure(tiny, sizeof(tiny), PUS_FAIL_LENGTH);
    }
    /* ...and consistent, but larger than the onboard buffer. */
    memset(tc, 0, sizeof(tc));
    memcpy(tc, TC_PING, 4);
    pus_put_u16(&tc[4], (uint16_t)(PUS_MAX_PACKET + 1u - 6u - 1u));
    expect_single_failure(tc, PUS_MAX_PACKET + 1u, PUS_FAIL_LENGTH);

    /* Check 5: a TM packet sent up the command link. */
    memcpy(tc, TC_PING, sizeof(TC_PING));
    tc[0] &= (uint8_t)~0x10u;
    fix_crc(tc, sizeof(TC_PING));
    expect_single_failure(tc, sizeof(TC_PING), PUS_FAIL_PACKET_HEADER);
    /* No secondary header flag. */
    memcpy(tc, TC_PING, sizeof(TC_PING));
    tc[0] &= (uint8_t)~0x08u;
    fix_crc(tc, sizeof(TC_PING));
    expect_single_failure(tc, sizeof(TC_PING), PUS_FAIL_PACKET_HEADER);
    /* Segmented (first segment). */
    memcpy(tc, TC_PING, sizeof(TC_PING));
    tc[2] = 0x40u;
    fix_crc(tc, sizeof(TC_PING));
    expect_single_failure(tc, sizeof(TC_PING), PUS_FAIL_PACKET_HEADER);

    /* Check 6: another application's APID. */
    n = pus_tc_build(0x011, 0, 0x9, 17, 1, 1, NULL, 0, tc, sizeof(tc));
    expect_single_failure(tc, (size_t)n, PUS_FAIL_APID);

    /* Check 7: PUS version 1 (PUS-A style header). */
    memcpy(tc, TC_PING, sizeof(TC_PING));
    tc[6] = 0x19u;
    fix_crc(tc, sizeof(TC_PING));
    expect_single_failure(tc, sizeof(TC_PING), PUS_FAIL_PUS_VERSION);

    /* Check 9: known service, unknown subtype. */
    n = pus_tc_build(APID_CMD, 0, 0x9, 17, 99, 1, NULL, 0, tc, sizeof(tc));
    expect_single_failure(tc, (size_t)n, PUS_FAIL_SUBTYPE);

    /* Check 10: TC(17,1) must not carry application data. */
    n = pus_tc_build(APID_CMD, 0, 0x9, 17, 1, 1, &one, 1, tc, sizeof(tc));
    expect_single_failure(tc, (size_t)n, PUS_FAIL_APP_DATA);
}

/* ---- Acceptance criterion 4: sequence counts --------------------------- */

static void test_sequence_count_increments_across_exchange(void)
{
    uint8_t tc[PUS_MAX_PACKET];
    uint8_t bad[sizeof(TC_PING)];
    int n, i;

    setup();
    tc_handle(&ctx, TC_PING, sizeof(TC_PING));               /* 3 TM */
    memcpy(bad, TC_PING, sizeof(bad)); bad[12] ^= 0xFFu;
    tc_handle(&ctx, bad, sizeof(bad));                       /* 1 TM */
    n = pus_tc_build(APID_CMD, 2, 0x9, 8, 1, 1, NULL, 0, tc, sizeof(tc));
    tc_handle(&ctx, tc, (size_t)n);                          /* 1 TM */
    n = pus_tc_build(APID_CMD, 3, 0x9, 17, 1, 1, NULL, 0, tc, sizeof(tc));
    tc_handle(&ctx, tc, (size_t)n);                          /* 3 TM */

    TEST_ASSERT_EQUAL_INT64((int64_t)(8), (int64_t)(cap_count));
    for (i = 0; i < cap_count && i < CAP_MAX; i++) {
        TEST_ASSERT_EQUAL_INT64((int64_t)(APID_CMD), (int64_t)(tm_apid(i)));
        TEST_ASSERT_EQUAL_INT64((int64_t)((unsigned)i), (int64_t)(tm_seq(i))); /* 0,1,2,... no gap, no repeat */
    }
    TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(ctx.accepted));
    TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(ctx.rejected));
}

/* ---- Execution failure path -------------------------------------------- */

static void test_execution_failure_reports_completion_failure(void)
{
    setup();
    cap_fail_from = 1; /* acceptance report gets out, test report does not */
    tc_handle(&ctx, TC_PING, sizeof(TC_PING));

    /* The sink refused TM(17,2), so service 17 reports failure and the
     * handler tries to send TM(1,8). In this test that is refused too. */
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(cap_count));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(tm_subtype(0)));

    /* The sequence counter still advanced for the lost packets, so the
     * ground would see the gap: accept(0), report(1), fail(2). */
    TEST_ASSERT_EQUAL_INT64((int64_t)(3), (int64_t)(ctx.tm_src.seq_count));
}

static pus_fail_t always_fail_check(const pus_tc_t *tc, void *arg)
{ (void)tc; (void)arg; return PUS_OK; }
static pus_fail_t always_fail_exec(const pus_tc_t *tc, tc_ctx_t *c, void *arg)
{ (void)tc; (void)c; (void)arg; return PUS_FAIL_EXECUTION; }

static void test_completion_failure_report_contents(void)
{
    uint8_t tc[PUS_MAX_PACKET];
    int n;

    setup();
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(tc_register_service(&ctx, 200, always_fail_check, always_fail_exec, NULL)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(-1), (int64_t)(tc_register_service(&ctx, 200, always_fail_check, always_fail_exec, NULL)));

    n = pus_tc_build(APID_CMD, 9, 0x9, 200, 1, 1, NULL, 0, tc, sizeof(tc));
    tc_handle(&ctx, tc, (size_t)n);

    TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(cap_count));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(tm_subtype(0)));                 /* accepted */
    TEST_ASSERT_EQUAL_INT64((int64_t)(8), (int64_t)(tm_subtype(1)));                 /* completion failure */
    TEST_ASSERT_EQUAL_MEMORY((tc), (tm_data(1)), (4));
    TEST_ASSERT_EQUAL_INT64((int64_t)(PUS_FAIL_EXECUTION), (int64_t)(tm_data(1)[4]));
}

/* ---- Added in Session 7 after the first MC/DC measurement ---------------
 * Defensive argument checks that no test exercised (coverage/mcdc.md,
 * gaps G-01..G-04): each condition now shown to matter on its own.       */

static void test_s7_tm_build_rejects_null_arguments(void)
{
    tm_source_t src = { APID_CMD, 0u };
    obt_t t = { 0u, 0u };
    uint8_t out[PUS_MAX_PACKET];
    const uint8_t data[2] = { 1u, 2u };
    TEST_ASSERT_TRUE(pus_tm_build(&src, 17, 2, 0, t, data, 2, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_INT(SP_ERR_ARG, pus_tm_build(NULL, 17, 2, 0, t, data, 2, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(SP_ERR_ARG, pus_tm_build(&src, 17, 2, 0, t, data, 2, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(SP_ERR_ARG, pus_tm_build(&src, 17, 2, 0, t, NULL, 2, out, sizeof(out)));
    TEST_ASSERT_TRUE(pus_tm_build(&src, 17, 2, 0, t, NULL, 0, out, sizeof(out)) > 0);
}

static void test_s7_tc_build_rejects_null_arguments(void)
{
    uint8_t out[PUS_MAX_PACKET];
    const uint8_t data[2] = { 1u, 2u };
    TEST_ASSERT_TRUE(pus_tc_build(APID_CMD, 1, 0x9, 8, 1, 1, data, 2, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_INT(SP_ERR_ARG, pus_tc_build(APID_CMD, 1, 0x9, 8, 1, 1, data, 2, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(SP_ERR_ARG, pus_tc_build(APID_CMD, 1, 0x9, 8, 1, 1, NULL, 2, out, sizeof(out)));
    TEST_ASSERT_TRUE(pus_tc_build(APID_CMD, 1, 0x9, 17, 1, 1, NULL, 0, out, sizeof(out)) > 0);
}

static void test_s7_tc_validate_rejects_null_arguments(void)
{
    uint8_t frame[PUS_MAX_PACKET];
    pus_tc_t tc;
    int n = pus_tc_build(APID_CMD, 1, 0x9, 17, 1, 1, NULL, 0, frame, sizeof(frame));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_INT(PUS_OK, pus_tc_validate(frame, (size_t)n, APID_CMD, &tc));
    TEST_ASSERT_EQUAL_INT(PUS_FAIL_NO_REPORT, pus_tc_validate(NULL, (size_t)n, APID_CMD, &tc));
    TEST_ASSERT_EQUAL_INT(PUS_FAIL_NO_REPORT, pus_tc_validate(frame, (size_t)n, APID_CMD, NULL));
}

static pus_fail_t dummy_check(const pus_tc_t *tc, void *arg)
{
    (void)tc; (void)arg;
    return PUS_OK;
}
static pus_fail_t dummy_exec(const pus_tc_t *tc, tc_ctx_t *c, void *arg)
{
    (void)tc; (void)c; (void)arg;
    return PUS_OK;
}

static void test_s7_register_service_rejects_null_and_full_table(void)
{
    uint8_t svc;
    setup();                                    /* service 17 registered */
    TEST_ASSERT_EQUAL_INT(-1, tc_register_service(&ctx, 100, NULL, dummy_exec, NULL));
    TEST_ASSERT_EQUAL_INT(-1, tc_register_service(&ctx, 100, dummy_check, NULL, NULL));
    for (svc = 100; ctx.service_count < TC_MAX_SERVICES; svc++) {
        TEST_ASSERT_EQUAL_INT(0, tc_register_service(&ctx, svc, dummy_check, dummy_exec, NULL));
    }
    TEST_ASSERT_EQUAL_INT(-1, tc_register_service(&ctx, 200, dummy_check, dummy_exec, NULL));
}

/* ---- Added in Session 7 after the coverage analysis (coverage.md, C-01) ---- */

static void test_s7_tm_build_rejects_too_small_output_buffer(void)
{
    tm_source_t src = { APID_CMD, 5u };
    obt_t t = { 0u, 0u };
    uint8_t out[PUS_TM_MIN_LEN];          /* no room for the 2 data octets */
    const uint8_t data[2] = { 1u, 2u };
    TEST_ASSERT_TRUE(pus_tm_build(&src, 17, 2, 0, t, data, 2, out, sizeof(out)) < 0);
    TEST_ASSERT_EQUAL_UINT16(5u, src.seq_count);   /* no packet, no count */
}

static void test_s7_tc_build_rejects_oversize_data_and_bad_flags(void)
{
    uint8_t out[PUS_MAX_PACKET + 8];
    uint8_t big[PUS_TC_MAX_DATA + 1u];
    memset(big, 0x55, sizeof(big));
    TEST_ASSERT_EQUAL_INT(SP_ERR_PAYLOAD,
                          pus_tc_build(APID_CMD, 1, 0x9, 17, 1, 1, big, sizeof(big), out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(SP_ERR_FIELD,
                          pus_tc_build(APID_CMD, 1, 0x10, 17, 1, 1, NULL, 0, out, sizeof(out)));
}

static void test_s7_emit_tm_returns_the_build_error(void)
{
    uint8_t data[PUS_TM_MAX_DATA + 1u];
    memset(data, 0, sizeof(data));
    setup();
    TEST_ASSERT_TRUE(tc_emit_tm(&ctx, 17, 2, 0, data, sizeof(data)) < 0);
    TEST_ASSERT_EQUAL_INT(0, cap_count);           /* nothing reached the sink */
}

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_check_value);
    RUN_TEST(test_tc_build_matches_hand_computed_octets);
    RUN_TEST(test_tm_build_matches_hand_computed_octets);
    RUN_TEST(test_tm_sequence_count_wraps_at_14_bits);
    RUN_TEST(test_tm_build_rejects_oversize_and_keeps_counter);
    RUN_TEST(test_obt_conversion);
    RUN_TEST(test_valid_ping_gives_accept_report_complete_in_order);
    RUN_TEST(test_ack_flags_select_success_reports);
    RUN_TEST(test_corrupted_checksum_is_rejected_and_not_executed);
    RUN_TEST(test_unimplemented_service_has_its_own_failure_code);
    RUN_TEST(test_validation_sequence_failure_codes);
    RUN_TEST(test_sequence_count_increments_across_exchange);
    RUN_TEST(test_execution_failure_reports_completion_failure);
    RUN_TEST(test_completion_failure_report_contents);
    RUN_TEST(test_s7_tm_build_rejects_null_arguments);
    RUN_TEST(test_s7_tc_build_rejects_null_arguments);
    RUN_TEST(test_s7_tc_validate_rejects_null_arguments);
    RUN_TEST(test_s7_register_service_rejects_null_and_full_table);
    RUN_TEST(test_s7_tm_build_rejects_too_small_output_buffer);
    RUN_TEST(test_s7_tc_build_rejects_oversize_data_and_bad_flags);
    RUN_TEST(test_s7_emit_tm_returns_the_build_error);
    return UNITY_END();
}
