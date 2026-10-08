/*
 * Lab 4.2 - host tests for the CRC, the PUS packet layer and the
 * telecommand handler. No RTEMS needed. Build and run: make test
 *
 * Reference CRC values were produced with an independent implementation
 * (Python binascii.crc_hqx, initial value 0xFFFF), not with crc16.c.
 */
#include "../crc16.h"
#include "../pus.h"
#include "../tc_handler.h"
#include "../../lab-4.1-space-packet/test_util.h"

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
    CHECK_EQ(crc16_ccitt((const uint8_t *)"123456789", 9), 0x29B1);
    CHECK_EQ(crc16_ccitt((const uint8_t *)"", 0), 0xFFFF);
    /* A packet followed by its own CRC has residue 0. */
    CHECK_EQ(crc16_ccitt(TC_PING, sizeof(TC_PING)), 0x0000);
}

/* ---- Packet builders --------------------------------------------------- */

static void test_tc_build_matches_hand_computed_octets(void)
{
    uint8_t out[PUS_MAX_PACKET];
    int n = pus_tc_build(APID_CMD, 0, PUS_ACK_ACCEPTANCE | PUS_ACK_COMPLETION,
                         17, 1, 0x0001, NULL, 0, out, sizeof(out));
    CHECK_EQ(n, 13);
    CHECK_MEM(out, TC_PING, sizeof(TC_PING));
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
    CHECK_EQ(n, 21);
    CHECK_MEM(out, expected, sizeof(expected));
    CHECK_EQ(src.seq_count, 2); /* consumed */
}

static void test_tm_sequence_count_wraps_at_14_bits(void)
{
    tm_source_t src = { APID_CMD, 0x3FFF };
    uint8_t out[PUS_MAX_PACKET];

    CHECK(pus_tm_build(&src, 17, 2, 0, fixed_now(), NULL, 0, out, sizeof(out)) > 0);
    CHECK_EQ(((out[2] & 0x3Fu) << 8) | out[3], 0x3FFF);
    CHECK_EQ(src.seq_count, 0);
}

static void test_tm_build_rejects_oversize_and_keeps_counter(void)
{
    static const uint8_t data[PUS_TM_MAX_DATA + 1u] = { 0 };
    tm_source_t src = { APID_CMD, 5 };
    uint8_t out[PUS_MAX_PACKET + 8u];

    CHECK(pus_tm_build(&src, 3, 25, 0, fixed_now(), data, sizeof(data),
                       out, sizeof(out)) < 0);
    CHECK_EQ(src.seq_count, 5); /* not consumed by a failed build */
    CHECK_EQ(pus_tm_build(&src, 3, 25, 0, fixed_now(), data, PUS_TM_MAX_DATA,
                          out, sizeof(out)), (long)PUS_MAX_PACKET);
}

static void test_obt_conversion(void)
{
    obt_t t = obt_from_ns(2500000000ull);
    CHECK_EQ(t.coarse, 2);
    CHECK_EQ(t.fine, 0x8000);
    t = obt_from_ns(999999999ull);
    CHECK_EQ(t.coarse, 0);
    CHECK_EQ(t.fine, 0xFFFF);
}

/* ---- Acceptance criterion 1: valid command ----------------------------- */

static void test_valid_ping_gives_accept_report_complete_in_order(void)
{
    setup();
    tc_handle(&ctx, TC_PING, sizeof(TC_PING));

    CHECK_EQ(cap_count, 3);
    CHECK_EQ(tm_service(0), 1);  CHECK_EQ(tm_subtype(0), 1); /* acceptance */
    CHECK_EQ(tm_service(1), 17); CHECK_EQ(tm_subtype(1), 2); /* test report */
    CHECK_EQ(tm_service(2), 1);  CHECK_EQ(tm_subtype(2), 7); /* completion */

    /* Verification reports carry the request ID = first 4 TC octets. */
    CHECK_EQ(tm_data_len(0), 4);
    CHECK_MEM(tm_data(0), TC_PING, 4);
    CHECK_EQ(tm_data_len(1), 0);
    CHECK_EQ(tm_data_len(2), 4);
    CHECK_MEM(tm_data(2), TC_PING, 4);

    /* Reports go back to the commanding source. */
    CHECK_EQ(tm_dest(0), 1); CHECK_EQ(tm_dest(1), 1); CHECK_EQ(tm_dest(2), 1);

    CHECK(tm_crc_ok(0)); CHECK(tm_crc_ok(1)); CHECK(tm_crc_ok(2));
    CHECK_EQ(ctx.accepted, 1);
    CHECK_EQ(ctx.rejected, 0);
}

static void test_ack_flags_select_success_reports(void)
{
    uint8_t tc[PUS_MAX_PACKET];
    int n;

    /* No flags: the command still runs, only the test report comes back. */
    setup();
    n = pus_tc_build(APID_CMD, 1, 0, 17, 1, 1, NULL, 0, tc, sizeof(tc));
    tc_handle(&ctx, tc, (size_t)n);
    CHECK_EQ(cap_count, 1);
    CHECK_EQ(tm_service(0), 17);

    /* All flags: acceptance, start, report, completion. No progress
     * report because the command has a single step. */
    setup();
    n = pus_tc_build(APID_CMD, 2, 0x0F, 17, 1, 1, NULL, 0, tc, sizeof(tc));
    tc_handle(&ctx, tc, (size_t)n);
    CHECK_EQ(cap_count, 4);
    CHECK_EQ(tm_subtype(0), 1);
    CHECK_EQ(tm_subtype(1), 3);
    CHECK_EQ(tm_service(2), 17);
    CHECK_EQ(tm_subtype(3), 7);
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

    CHECK_EQ(cap_count, 1);                     /* nothing else: no TM(17,2) */
    CHECK_EQ(tm_service(0), 1);
    CHECK_EQ(tm_subtype(0), 2);                 /* acceptance failure */
    CHECK_EQ(tm_data_len(0), 5);
    CHECK_MEM(tm_data(0), TC_PING, 4);
    CHECK_EQ(tm_data(0)[4], PUS_FAIL_CHECKSUM);
    CHECK_EQ(tm_dest(0), 0);                    /* source ID not trusted */
    CHECK_EQ(ctx.accepted, 0);
    CHECK_EQ(ctx.rejected, 1);

    /* Any single bit error in the PUS header or CRC must be caught and
     * must never produce a test report. (Bits in the primary header are
     * covered too, but can legitimately fail an earlier check.) */
    for (bit = 0; bit < sizeof(TC_PING) * 8u; bit++) {
        int i;
        setup();
        memcpy(bad, TC_PING, sizeof(bad));
        bad[bit / 8u] ^= (uint8_t)(0x80u >> (bit % 8u));
        tc_handle(&ctx, bad, sizeof(bad));
        CHECK_EQ(cap_count, 1);
        CHECK_EQ(tm_subtype(0), 2);
        for (i = 0; i < cap_count && i < CAP_MAX; i++) {
            CHECK(tm_service(i) != 17);
        }
        if (bit >= 6u * 8u) {
            CHECK_EQ(tm_data(0)[4], PUS_FAIL_CHECKSUM);
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

    CHECK_EQ(cap_count, 1);
    CHECK_EQ(tm_service(0), 1);
    CHECK_EQ(tm_subtype(0), 2);
    CHECK_EQ(tm_data(0)[4], PUS_FAIL_SERVICE);
    CHECK(PUS_FAIL_SERVICE != PUS_FAIL_CHECKSUM);
    CHECK_MEM(tm_data(0), tc, 4);
    CHECK_EQ(tm_dest(0), 1); /* packet was intact, so the source is known */
}

/* ---- Every other check in the validation sequence ---------------------- */

static void expect_single_failure(const uint8_t *frame, size_t len,
                                  pus_fail_t code)
{
    setup();
    tc_handle(&ctx, frame, len);
    CHECK_EQ(cap_count, 1);
    CHECK_EQ(tm_service(0), 1);
    CHECK_EQ(tm_subtype(0), 2);
    CHECK_EQ(tm_data(0)[4], code);
    CHECK_EQ(ctx.accepted, 0);
    CHECK_EQ(ctx.rejected, 1);
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
        CHECK_EQ(cap_count, 0);
        CHECK_EQ(ctx.rejected, 1);
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

    CHECK_EQ(cap_count, 8);
    for (i = 0; i < cap_count && i < CAP_MAX; i++) {
        CHECK_EQ(tm_apid(i), APID_CMD);
        CHECK_EQ(tm_seq(i), (unsigned)i); /* 0,1,2,... no gap, no repeat */
    }
    CHECK_EQ(ctx.accepted, 2);
    CHECK_EQ(ctx.rejected, 2);
}

/* ---- Execution failure path -------------------------------------------- */

static void test_execution_failure_reports_completion_failure(void)
{
    setup();
    cap_fail_from = 1; /* acceptance report gets out, test report does not */
    tc_handle(&ctx, TC_PING, sizeof(TC_PING));

    /* The sink refused TM(17,2), so service 17 reports failure and the
     * handler tries to send TM(1,8). In this test that is refused too. */
    CHECK_EQ(cap_count, 1);
    CHECK_EQ(tm_subtype(0), 1);

    /* The sequence counter still advanced for the lost packets, so the
     * ground would see the gap: accept(0), report(1), fail(2). */
    CHECK_EQ(ctx.tm_src.seq_count, 3);
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
    CHECK_EQ(tc_register_service(&ctx, 200, always_fail_check, always_fail_exec, NULL), 0);
    CHECK_EQ(tc_register_service(&ctx, 200, always_fail_check, always_fail_exec, NULL), -1);

    n = pus_tc_build(APID_CMD, 9, 0x9, 200, 1, 1, NULL, 0, tc, sizeof(tc));
    tc_handle(&ctx, tc, (size_t)n);

    CHECK_EQ(cap_count, 2);
    CHECK_EQ(tm_subtype(0), 1);                 /* accepted */
    CHECK_EQ(tm_subtype(1), 8);                 /* completion failure */
    CHECK_MEM(tm_data(1), tc, 4);
    CHECK_EQ(tm_data(1)[4], PUS_FAIL_EXECUTION);
}

int main(void)
{
    printf("Lab 4.2 - PUS layer and telecommand handler tests\n\n");
    RUN(test_crc_check_value);
    RUN(test_tc_build_matches_hand_computed_octets);
    RUN(test_tm_build_matches_hand_computed_octets);
    RUN(test_tm_sequence_count_wraps_at_14_bits);
    RUN(test_tm_build_rejects_oversize_and_keeps_counter);
    RUN(test_obt_conversion);
    RUN(test_valid_ping_gives_accept_report_complete_in_order);
    RUN(test_ack_flags_select_success_reports);
    RUN(test_corrupted_checksum_is_rejected_and_not_executed);
    RUN(test_unimplemented_service_has_its_own_failure_code);
    RUN(test_validation_sequence_failure_codes);
    RUN(test_sequence_count_increments_across_exchange);
    RUN(test_execution_failure_reports_completion_failure);
    RUN(test_completion_failure_report_contents);
    return TEST_SUMMARY();
}
