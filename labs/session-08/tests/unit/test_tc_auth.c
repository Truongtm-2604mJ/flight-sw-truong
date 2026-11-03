/* Unit suite: telecommand authentication through the Session 4 handler
 * (tc_auth.c + tc_handler.c), service 210 (sec_service.c) and the rate
 * limited reporter (sec_report.c).
 *
 * The five cases Lab 8.1 asks for are marked [8.1-1] .. [8.1-5]. */
#include "crc16.h"
#include "keystore.h"
#include "pus.h"
#include "sec_report.h"
#include "sec_service.h"
#include "tc_auth.h"
#include "tc_handler.h"
#include "unity.h"

#include <string.h>

#define APID 0x010u
#define SVC_STUB 99u

static keystore_t ks;
static tc_auth_t auth;
static sec_service_t svc;
static tc_ctx_t ctx;
static int executed;
static pus_fail_t exec_result;
static int persisted;

/* ---- TM capture ---------------------------------------------------------- */
static int tm_count, tm_12, tm_11, tm_17, tm_18;
static uint8_t tm_fail_code;
static int sink(const uint8_t *p, size_t n, void *arg)
{
    (void)arg; (void)n;
    tm_count++;
    if (p[7] == 1u) {
        if (p[8] == 1u) { tm_11++; }
        if (p[8] == 2u) { tm_12++; tm_fail_code = p[6 + 13 + 4]; }
        if (p[8] == 7u) { tm_17++; }
        if (p[8] == 8u) { tm_18++; tm_fail_code = p[6 + 13 + 4]; }
    }
    return 0;
}
static obt_t now(void) { obt_t t = { 0u, 0u }; return t; }

/* ---- A stub service: subtype 1 executes, subtype 2 fails execution ---------- */
static pus_fail_t stub_check(const pus_tc_t *tc, void *arg)
{
    (void)arg;
    return (tc->subtype == 1u || tc->subtype == 2u) && tc->app_len == 2u
               ? PUS_OK : PUS_FAIL_SUBTYPE;
}
static pus_fail_t stub_exec(const pus_tc_t *tc, struct tc_ctx *c, void *arg)
{
    (void)c; (void)arg;
    executed++;
    return tc->subtype == 2u ? PUS_FAIL_EXECUTION : exec_result;
}
static void on_persist(const keystore_t *k, void *arg) { (void)k; (void)arg; persisted++; }

void setUp(void)
{
    ks_factory(&ks);
    ks_counter_floor(&ks, 1u, 0u);      /* commissioned: first contact done */
    tc_auth_init(&auth, &ks, on_persist, NULL);
    sec_service_init(&svc, &auth);
    tc_init(&ctx, APID, sink, NULL, now);
    TEST_ASSERT_EQUAL_INT(0, tc_register_service(&ctx, SVC_STUB, stub_check, stub_exec, NULL));
    TEST_ASSERT_EQUAL_INT(0, sec_service_register(&svc, &ctx));
    ctx.security = tc_auth_hook(&auth);
    executed = 0; exec_result = PUS_OK; persisted = 0;
    tm_count = tm_12 = tm_11 = tm_17 = tm_18 = 0; tm_fail_code = 0;
}
void tearDown(void) { }

/* ---- Building secured TCs (the MAC through a key store holding `key`) ------- */
static size_t secure_tc(uint8_t *out, uint8_t service, uint8_t subtype,
                        const uint8_t *data, size_t len, uint8_t spi, uint32_t seq,
                        const keystore_t *mac_ks)
{
    uint8_t app[PUS_TC_MAX_DATA];
    size_t trailer, n;
    int r;
    uint16_t crc;

    memcpy(app, data, len);
    app[len] = spi;
    app[len + 1u] = (uint8_t)(seq >> 24); app[len + 2u] = (uint8_t)(seq >> 16);
    app[len + 3u] = (uint8_t)(seq >> 8);  app[len + 4u] = (uint8_t)seq;
    memset(&app[len + 5u], 0, KS_MAC_LEN);
    r = pus_tc_build(APID, 0, 0x9u, service, subtype, 1u, app, len + SEC_TRAILER_LEN,
                     out, PUS_MAX_PACKET);
    TEST_ASSERT_TRUE(r > 0);
    n = (size_t)r;
    trailer = 6u + 5u + len;
    TEST_ASSERT_EQUAL_INT(0, ks_mac(mac_ks, spi, out, trailer + 5u, &out[trailer + 5u]));
    crc = crc16_ccitt(out, n - 2u);
    out[n - 2u] = (uint8_t)(crc >> 8);
    out[n - 1u] = (uint8_t)crc;
    return n;
}
static size_t stub_tc(uint8_t *out, uint32_t seq)
{
    static const uint8_t d[2] = { 0xAAu, 0x55u };
    return secure_tc(out, SVC_STUB, 1u, d, 2u, ks.active, seq, &ks);
}
static uint32_t counter(uint8_t slot)
{
    uint32_t v = 0u;
    (void)ks_counter(&ks, slot, &v);
    return v;
}

/* [8.1-1] valid command executes and advances the counter */
static void test_valid_executes_and_advances(void)
{
    uint8_t f[PUS_MAX_PACKET];
    size_t n = stub_tc(f, 1u);
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_INT(1, executed);
    TEST_ASSERT_EQUAL_UINT32(1u, counter(1));
    TEST_ASSERT_EQUAL_INT(1, tm_11);
    TEST_ASSERT_EQUAL_INT(1, tm_17);
    TEST_ASSERT_EQUAL_INT(1, persisted);
    n = stub_tc(f, 40u);                              /* gaps are fine */
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_INT(2, executed);
    TEST_ASSERT_EQUAL_UINT32(40u, counter(1));
    TEST_ASSERT_EQUAL_UINT32(2u, auth.auth_accepted);
}

/* [8.1-2] wrong key: rejected at authentication, never executes */
static void test_wrong_key_rejected_at_auth(void)
{
    static const uint8_t d[2] = { 0xAAu, 0x55u };
    keystore_t other;
    uint8_t f[PUS_MAX_PACKET];
    size_t n;

    ks_factory(&other);
    other.slot[1].key[0] ^= 0x01u;                    /* a different key */
    n = secure_tc(f, SVC_STUB, 1u, d, 2u, 1u, 1u, &other);
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_INT(0, executed);
    TEST_ASSERT_EQUAL_UINT32(1u, auth.rej_total[SEC_REJ_AUTH]);
    TEST_ASSERT_EQUAL_UINT8(PUS_FAIL_AUTH, auth.rej_last_code[SEC_REJ_AUTH]);
    TEST_ASSERT_EQUAL_INT(0, tm_count);               /* no TM(1,2): no oracle */
    TEST_ASSERT_EQUAL_UINT32(0u, counter(1));
    TEST_ASSERT_EQUAL_INT(0, persisted);
    /* One flipped bit anywhere in the authenticated part. */
    n = stub_tc(f, 1u);
    f[12] ^= 0x01u;                                   /* app data */
    {
        uint16_t crc = crc16_ccitt(f, n - 2u);
        f[n - 2u] = (uint8_t)(crc >> 8); f[n - 1u] = (uint8_t)crc;
    }
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_INT(0, executed);
    TEST_ASSERT_EQUAL_UINT32(2u, auth.rej_total[SEC_REJ_AUTH]);
}

/* [8.1-3] replay rejected */
static void test_replay_rejected(void)
{
    uint8_t f[PUS_MAX_PACKET];
    size_t n = stub_tc(f, 5u);
    tc_handle(&ctx, f, n);
    tc_handle(&ctx, f, n);                            /* the same packet */
    TEST_ASSERT_EQUAL_INT(1, executed);
    TEST_ASSERT_EQUAL_UINT32(1u, auth.rej_total[SEC_REJ_REPLAY]);
    n = stub_tc(f, 4u);                               /* older, never seen */
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_INT(1, executed);
    TEST_ASSERT_EQUAL_UINT32(2u, auth.rej_total[SEC_REJ_REPLAY]);
    TEST_ASSERT_EQUAL_UINT32(5u, counter(1));
    TEST_ASSERT_EQUAL_INT(2, tm_count);               /* TM(1,1), TM(1,7) only */
}

/* [8.1-4] far future counter: policy = reject (WINDOW), never jump ahead */
static void test_far_future_rejected_by_window(void)
{
    uint8_t f[PUS_MAX_PACKET];
    size_t n = stub_tc(f, 10u);
    tc_handle(&ctx, f, n);
    n = stub_tc(f, 10u + KS_WINDOW + 1u);
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_INT(1, executed);
    TEST_ASSERT_EQUAL_UINT32(1u, auth.rej_total[SEC_REJ_WINDOW]);
    TEST_ASSERT_EQUAL_UINT32(10u, counter(1));        /* unchanged */
    n = stub_tc(f, 0xFFFFFFFFu);                      /* would end the key */
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_UINT32(2u, auth.rej_total[SEC_REJ_WINDOW]);
    n = stub_tc(f, 10u + KS_WINDOW);                  /* edge: accepted */
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_INT(2, executed);
    TEST_ASSERT_EQUAL_UINT32(10u + KS_WINDOW, counter(1));
}

/* [8.1-5] a burst of hundreds of invalid commands: bounded events */
static int events_seen;
static int first_seen[SEC_REJ_CLASSES], summary_seen[SEC_REJ_CLASSES];
static uint32_t summary_count[SEC_REJ_CLASSES];
static void count_event(uint8_t sev, const uint8_t *aux, size_t len, void *arg)
{
    uint16_t id = (uint16_t)((aux[0] << 8) | aux[1]);
    (void)sev; (void)len; (void)arg;
    events_seen++;
    if (id == SEC_EVT_REJECT_FIRST) { first_seen[aux[2]]++; }
    if (id == SEC_EVT_REJECT_SUMMARY) {
        summary_seen[aux[2]]++;
        summary_count[aux[2]] += ((uint32_t)aux[4] << 24) | ((uint32_t)aux[5] << 16) |
                                 ((uint32_t)aux[6] << 8) | aux[7];
    }
}
static void test_burst_gives_bounded_events(void)
{
    static const uint8_t d[2] = { 0xAAu, 0x55u };
    keystore_t other;
    sec_report_t rep;
    uint8_t f[PUS_MAX_PACKET];
    size_t n;
    uint32_t t_ms;
    int i;

    ks_factory(&other);
    other.slot[1].key[3] ^= 0x40u;
    sec_report_init(&rep);
    events_seen = 0;
    memset(first_seen, 0, sizeof(first_seen));
    memset(summary_seen, 0, sizeof(summary_seen));
    memset(summary_count, 0, sizeof(summary_count));

    /* 600 forged commands within 3 s (the reporter runs every 500 ms). */
    t_ms = 0u;
    for (i = 0; i < 600; i++) {
        n = secure_tc(f, SVC_STUB, 1u, d, 2u, 1u, (uint32_t)i + 1u, &other);
        tc_handle(&ctx, f, n);
        if (i % 100 == 99) {
            sec_report_cycle(&rep, auth.rej_total, auth.rej_last_code, t_ms,
                             count_event, NULL);
            t_ms += 500u;
        }
    }
    TEST_ASSERT_EQUAL_INT(0, executed);
    TEST_ASSERT_EQUAL_INT(0, tm_count);
    TEST_ASSERT_EQUAL_UINT32(600u, auth.rej_total[SEC_REJ_AUTH]);
    TEST_ASSERT_EQUAL_INT(1, events_seen);            /* the FIRST only, so far */
    /* Let 30 s pass with no more traffic. */
    for (; t_ms <= 33000u; t_ms += 500u) {
        sec_report_cycle(&rep, auth.rej_total, auth.rej_last_code, t_ms,
                         count_event, NULL);
    }
    TEST_ASSERT_EQUAL_INT(1, first_seen[SEC_REJ_AUTH]);
    TEST_ASSERT_EQUAL_INT(1, summary_seen[SEC_REJ_AUTH]);
    TEST_ASSERT_EQUAL_UINT32(599u, summary_count[SEC_REJ_AUTH]);  /* nothing lost */
    TEST_ASSERT_EQUAL_INT(2, events_seen);
    TEST_ASSERT_EQUAL_UINT32(0u, counter(1));
}

/* A flood lasting 60 s: at most 1 + 60/10 + 1 events per class. */
static void test_sustained_flood_bounded(void)
{
    sec_report_t rep;
    uint32_t totals[SEC_REJ_CLASSES] = { 0 };
    uint8_t codes[SEC_REJ_CLASSES] = { 0 };
    uint32_t t;

    sec_report_init(&rep);
    events_seen = 0;
    memset(summary_count, 0, sizeof(summary_count));
    memset(first_seen, 0, sizeof(first_seen));
    for (t = 0; t <= 80000u; t += 500u) {
        if (t < 60000u) {
            totals[SEC_REJ_AUTH] += 50u;    /* 100 per second */
            totals[SEC_REJ_CRC]  += 3u;
        }
        sec_report_cycle(&rep, totals, codes, t, count_event, NULL);
    }
    TEST_ASSERT_TRUE(events_seen <= 2 * (1 + 60 / 10 + 1));
    TEST_ASSERT_EQUAL_INT(1, first_seen[SEC_REJ_AUTH]);
    TEST_ASSERT_EQUAL_UINT32(totals[SEC_REJ_AUTH] - 1u, summary_count[SEC_REJ_AUTH]);
    TEST_ASSERT_EQUAL_UINT32(totals[SEC_REJ_CRC] - 1u, summary_count[SEC_REJ_CRC]);
    /* After a quiet window the class is closed: a new rejection is FIRST. */
    totals[SEC_REJ_AUTH]++;
    sec_report_cycle(&rep, totals, codes, 90000u, count_event, NULL);
    TEST_ASSERT_EQUAL_INT(2, first_seen[SEC_REJ_AUTH]);
}

/* Check order: checksum before authentication, structure before both. */
static void test_check_order(void)
{
    uint8_t f[PUS_MAX_PACKET];
    size_t n = stub_tc(f, 1u);
    f[n - 1u] ^= 0x01u;                               /* CRC only */
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_UINT32(1u, auth.rej_total[SEC_REJ_CRC]);
    TEST_ASSERT_EQUAL_UINT32(0u, auth.rej_total[SEC_REJ_AUTH]);
    n = stub_tc(f, 1u);
    f[1] ^= 0x01u;                                    /* APID: structural */
    {
        uint16_t crc = crc16_ccitt(f, n - 2u);
        f[n - 2u] = (uint8_t)(crc >> 8); f[n - 1u] = (uint8_t)crc;
    }
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_UINT32(1u, auth.rej_total[SEC_REJ_STRUCTURE]);
    TEST_ASSERT_EQUAL_INT(0, executed);
    TEST_ASSERT_EQUAL_INT(0, tm_count);
    TEST_ASSERT_EQUAL_UINT8(SEC_REJ_STRUCTURE, tc_auth_class(PUS_FAIL_NO_REPORT));
    TEST_ASSERT_EQUAL_UINT8(SEC_REJ_STATE, tc_auth_class(PUS_FAIL_SEC_STATE));
}

/* No trailer at all (a Session 5 style TC): structural, not executed. */
static void test_plain_tc_rejected(void)
{
    uint8_t f[PUS_MAX_PACKET];
    int n = pus_tc_build(APID, 0, 0x9u, 17u, 1u, 1u, NULL, 0, f, sizeof(f));
    tc_handle(&ctx, f, (size_t)n);
    TEST_ASSERT_EQUAL_UINT32(1u, auth.rej_total[SEC_REJ_STRUCTURE]);
    TEST_ASSERT_EQUAL_INT(0, tm_count);
}

/* Counter updated only after FULL acceptance. */
static void test_counter_not_advanced_by_semantic_reject(void)
{
    static const uint8_t d[2] = { 0xAAu, 0x55u };
    uint8_t f[PUS_MAX_PACKET];
    size_t n = secure_tc(f, SVC_STUB, 7u, d, 2u, 1u, 3u, &ks);  /* bad subtype */
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_INT(1, tm_12);                  /* authenticated: answered */
    TEST_ASSERT_EQUAL_UINT8(PUS_FAIL_SUBTYPE, tm_fail_code);
    TEST_ASSERT_EQUAL_UINT32(0u, counter(1));
    TEST_ASSERT_EQUAL_INT(0, persisted);
    n = stub_tc(f, 3u);                               /* same counter value */
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_INT(1, executed);
    TEST_ASSERT_EQUAL_UINT32(3u, counter(1));
    /* An execution failure is after acceptance: the counter has moved. */
    n = secure_tc(f, SVC_STUB, 2u, d, 2u, 1u, 4u, &ks);
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_INT(1, tm_18);
    TEST_ASSERT_EQUAL_UINT32(4u, counter(1));
}

static void test_spi_rules(void)
{
    static const uint8_t d[2] = { 0xAAu, 0x55u };
    uint8_t f[PUS_MAX_PACKET];
    size_t n;

    n = secure_tc(f, SVC_STUB, 1u, d, 2u, 0u, 1u, &ks);   /* recovery key, normal TC */
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_UINT32(1u, auth.rej_total[SEC_REJ_STATE]);
    ks.slot[2] = ks.slot[1];
    ks.slot[2].state = KS_LOADED;                     /* a spare, not active */
    n = secure_tc(f, SVC_STUB, 1u, d, 2u, 2u, 1u, &ks);
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_UINT32(2u, auth.rej_total[SEC_REJ_STATE]);
    f[6 + 5 + 2] = 9u;                                /* SPI 9 */
    {
        uint16_t crc = crc16_ccitt(f, n - 2u);
        f[n - 2u] = (uint8_t)(crc >> 8); f[n - 1u] = (uint8_t)crc;
    }
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_UINT32(3u, auth.rej_total[SEC_REJ_STATE]);
    TEST_ASSERT_EQUAL_INT(0, executed);
}

/* Released from the onboard schedule: no trailer, counter untouched. */
static void test_trusted_release(void)
{
    static const uint8_t d[2] = { 0xAAu, 0x55u };
    uint8_t f[PUS_MAX_PACKET];
    int n = pus_tc_build(APID, 0, 0x9u, SVC_STUB, 1u, 1u, d, 2u, f, sizeof(f));
    auth.trusted = 1;
    tc_handle(&ctx, f, (size_t)n);
    auth.trusted = 0;
    TEST_ASSERT_EQUAL_INT(1, executed);
    TEST_ASSERT_EQUAL_UINT32(0u, counter(1));
    TEST_ASSERT_EQUAL_INT(0, persisted);
    tc_handle(&ctx, f, (size_t)n);                    /* not trusted any more */
    TEST_ASSERT_EQUAL_INT(1, executed);
}

/* ---- Service 210 ---------------------------------------------------------------- */

static const uint8_t NONCE[KS_NONCE_LEN] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
static const uint8_t WRAPPED_SLOT2[KS_WRAPPED_LEN] = {
    0xC9u, 0xAAu, 0xCBu, 0x8Cu, 0xE7u, 0xB8u, 0x61u, 0xE6u, 0x93u, 0x3Fu,
    0x17u, 0x2Fu, 0xF8u, 0x7Eu, 0xD4u, 0x77u, 0xB5u, 0x56u, 0xBCu, 0x07u,
    0xF6u, 0xBBu, 0x81u, 0xF8u, 0xB2u, 0x41u, 0xFCu, 0x63u, 0x84u, 0xA7u,
    0x88u, 0x16u, 0x92u, 0xD6u, 0x63u, 0x0Eu, 0xD8u, 0x02u, 0xF2u, 0xF2u,
    0xB5u, 0x9Fu, 0x9Du, 0xA9u, 0x6Eu, 0x69u, 0x2Fu, 0x5Fu
};

static uint16_t take_event(void)
{
    uint8_t ev[SEC_EV_MAX], sev;
    size_t n = sec_service_take(&svc, &sev, ev);
    return n == 0u ? 0u : (uint16_t)((ev[0] << 8) | ev[1]);
}

static void send210(uint8_t sub, const uint8_t *d, size_t len, uint8_t spi, uint32_t seq)
{
    uint8_t f[PUS_MAX_PACKET];
    size_t n = secure_tc(f, SEC_SVC, sub, d, len, spi, seq, &ks);
    tc_handle(&ctx, f, n);
}

static void load_slot2(uint32_t seq)
{
    uint8_t d[1 + KS_NONCE_LEN + KS_WRAPPED_LEN];
    d[0] = 2u;
    memcpy(&d[1], NONCE, KS_NONCE_LEN);
    memcpy(&d[1 + KS_NONCE_LEN], WRAPPED_SLOT2, KS_WRAPPED_LEN);
    send210(SEC_TC_LOAD_KEY, d, sizeof(d), 1u, seq);
}

static void test_rekey_load_then_activate(void)
{
    uint8_t slot = 2u;
    uint8_t f[PUS_MAX_PACKET];
    size_t n;

    load_slot2(1u);
    TEST_ASSERT_EQUAL_INT(1, tm_17);
    TEST_ASSERT_EQUAL_UINT8(KS_LOADED, ks.slot[2].state);
    TEST_ASSERT_EQUAL_UINT8(1, ks.active);            /* loading is not switching */
    TEST_ASSERT_EQUAL_HEX16(SEC_EVT_KEY_LOADED, take_event());
    send210(SEC_TC_ACTIVATE, &slot, 1u, 1u, 2u);
    TEST_ASSERT_EQUAL_UINT8(2, ks.active);
    TEST_ASSERT_EQUAL_HEX16(SEC_EVT_KEY_ACTIVATED, take_event());
    TEST_ASSERT_EQUAL_UINT32(2u, counter(1));         /* the activate committed */
    /* The old key no longer opens anything; the new one, from counter 1. */
    n = secure_tc(f, SVC_STUB, 1u, (const uint8_t *)"\xAA\x55", 2u, 1u, 3u, &ks);
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_UINT32(1u, auth.rej_total[SEC_REJ_STATE]);
    n = stub_tc(f, 1u);
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_INT(1, executed);
    TEST_ASSERT_EQUAL_UINT32(1u, counter(2));
}

static void test_rekey_bad_wrap_fails_execution(void)
{
    uint8_t d[1 + KS_NONCE_LEN + KS_WRAPPED_LEN];
    d[0] = 2u;
    memcpy(&d[1], NONCE, KS_NONCE_LEN);
    memcpy(&d[1 + KS_NONCE_LEN], WRAPPED_SLOT2, KS_WRAPPED_LEN);
    d[20] ^= 0x01u;
    send210(SEC_TC_LOAD_KEY, d, sizeof(d), 1u, 1u);
    TEST_ASSERT_EQUAL_INT(1, tm_18);                  /* TM(1,8) execution */
    TEST_ASSERT_EQUAL_UINT8(KS_EMPTY, ks.slot[2].state);
    TEST_ASSERT_EQUAL_HEX16(SEC_EVT_KEY_LOAD_FAILED, take_event());
}

static void test_210_acceptance_checks(void)
{
    uint8_t slot = 1u;
    uint8_t d5[5] = { 2u, 0, 0, 0, 9 };
    send210(SEC_TC_ACTIVATE, &slot, 1u, 1u, 1u);      /* already active */
    TEST_ASSERT_EQUAL_INT(1, tm_12);
    slot = 2u;
    send210(SEC_TC_ACTIVATE, &slot, 1u, 1u, 2u);      /* empty */
    TEST_ASSERT_EQUAL_INT(2, tm_12);
    slot = 1u;
    send210(SEC_TC_ERASE, &slot, 1u, 1u, 3u);         /* active: refused */
    TEST_ASSERT_EQUAL_INT(3, tm_12);
    TEST_ASSERT_EQUAL_UINT8(KS_ACTIVE, ks.slot[1].state);
    send210(SEC_TC_RECOVERY_COUNTER, d5, 5u, 1u, 4u); /* operational key: no */
    TEST_ASSERT_EQUAL_INT(4, tm_12);
    TEST_ASSERT_EQUAL_UINT8(PUS_FAIL_SEC_STATE, tm_fail_code);
    send210(77u, &slot, 1u, 1u, 5u);
    TEST_ASSERT_EQUAL_UINT8(PUS_FAIL_SUBTYPE, tm_fail_code);
    TEST_ASSERT_EQUAL_UINT32(0u, counter(1));         /* none was accepted */
}

/* Counter corrupted high: every command is a replay. Recovery resets it. */
static void test_recovery_reset_counter(void)
{
    uint8_t d5[5] = { 1u, 0, 0, 0x01u, 0x00u };       /* slot 1, floor 256 */
    uint8_t f[PUS_MAX_PACKET];
    uint32_t chal;
    size_t n;

    ks_counter_set(&ks, 1, 0xFFFFFFF0u);
    n = stub_tc(f, 300u);
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_UINT32(1u, auth.rej_total[SEC_REJ_REPLAY]);

    send210(SEC_TC_RECOVERY_COUNTER, d5, 5u, 0u, 2u); /* wrong challenge */
    TEST_ASSERT_EQUAL_UINT32(1u, auth.rej_total[SEC_REJ_WINDOW]);
    (void)ks_challenge(&ks, &chal);
    TEST_ASSERT_EQUAL_UINT32(1u, chal);
    send210(SEC_TC_RECOVERY_COUNTER, d5, 5u, 0u, chal);
    TEST_ASSERT_EQUAL_INT(1, tm_17);
    TEST_ASSERT_EQUAL_UINT32(256u, counter(1));
    TEST_ASSERT_EQUAL_HEX16(SEC_EVT_RECOVERY_USED, take_event());
    TEST_ASSERT_EQUAL_HEX16(SEC_EVT_COUNTER_SET, take_event());
    (void)ks_challenge(&ks, &chal);
    TEST_ASSERT_EQUAL_UINT32(2u, chal);               /* one use per challenge */
    send210(SEC_TC_RECOVERY_COUNTER, d5, 5u, 0u, 1u); /* replayed recovery */
    TEST_ASSERT_EQUAL_UINT32(2u, auth.rej_total[SEC_REJ_REPLAY]);
    n = stub_tc(f, 300u);                             /* back in service */
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_INT(1, executed);
}

/* Active key lost to an upset: recovery activates the spare. */
static void test_recovery_activate_spare(void)
{
    uint8_t slot = 2u;
    uint8_t f[PUS_MAX_PACKET];
    size_t n;

    load_slot2(1u);
    (void)take_event();
    ks.slot[1].key[0] ^= 1u; ks.slot[1].key_copy[1][0] ^= 1u;
    TEST_ASSERT_EQUAL_UINT(1u << 1, ks_scrub(&ks));
    {
        keystore_t factory;                           /* right MAC, SPI 1 */
        ks_factory(&factory);
        n = secure_tc(f, SVC_STUB, 1u, (const uint8_t *)"\xAA\x55", 2u, 1u, 2u, &factory);
    }
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_UINT32(1u, auth.rej_total[SEC_REJ_STATE]);
    send210(SEC_TC_ACTIVATE, &slot, 1u, 2u, 1u);      /* spare can't do it */
    TEST_ASSERT_EQUAL_UINT32(2u, auth.rej_total[SEC_REJ_STATE]);
    send210(SEC_TC_RECOVERY_ACTIVATE, &slot, 1u, 0u, 1u);
    TEST_ASSERT_EQUAL_UINT8(2, ks.active);
    TEST_ASSERT_EQUAL_UINT8(KS_CORRUPT, ks.slot[1].state);
    TEST_ASSERT_EQUAL_HEX16(SEC_EVT_RECOVERY_USED, take_event());
    TEST_ASSERT_EQUAL_HEX16(SEC_EVT_KEY_ACTIVATED, take_event());
    n = stub_tc(f, 1u);
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_INT(1, executed);
    /* The old key, even correctly MACed, is refused: its slot is corrupt. */
    {
        keystore_t factory;
        ks_factory(&factory);
        n = secure_tc(f, SVC_STUB, 1u, (const uint8_t *)"\xAA\x55", 2u, 1u, 9u, &factory);
        tc_handle(&ctx, f, n);
    }
    /* The recovery key cannot load keys, change modes, or anything else. */
    send210(SEC_TC_LOAD_KEY, &slot, 1u, 0u, 2u);
    TEST_ASSERT_EQUAL_UINT32(4u, auth.rej_total[SEC_REJ_STATE]);
    TEST_ASSERT_EQUAL_INT(1, executed);
}

/* Factory state (blank NV): slot 1 refuses everything until the ground sets
 * a floor with the recovery key, so NV loss cannot re-open old commands. */
static void test_first_contact_after_factory(void)
{
    uint8_t d5[5] = { 1u, 0, 0, 0x10u, 0x00u };       /* slot 1, floor 4096 */
    uint8_t f[PUS_MAX_PACKET];
    size_t n;

    ks_factory(&ks);
    TEST_ASSERT_TRUE(ks_counter_untrusted(&ks, 1u));
    n = stub_tc(f, 5000u);                            /* a recorded old command */
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_INT(0, executed);
    TEST_ASSERT_EQUAL_UINT32(1u, auth.rej_total[SEC_REJ_STATE]);
    send210(SEC_TC_RECOVERY_COUNTER, d5, 5u, 0u, 1u); /* first contact */
    TEST_ASSERT_EQUAL_INT(1, tm_17);
    TEST_ASSERT_FALSE(ks_counter_untrusted(&ks, 1u));
    TEST_ASSERT_EQUAL_UINT32(4096u, counter(1));
    n = stub_tc(f, 4000u);                            /* below the floor */
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_UINT32(1u, auth.rej_total[SEC_REJ_REPLAY]);
    n = stub_tc(f, 4097u);
    tc_handle(&ctx, f, n);
    TEST_ASSERT_EQUAL_INT(1, executed);
}

static void test_event_queue_bounded(void)
{
    uint8_t ev[4] = { 0x06u, 0x08u, 1u, 0u };
    unsigned i;
    for (i = 0; i < SEC_EVQ_DEPTH + 3u; i++) {
        sec_service_queue(&svc, 1u, ev, sizeof(ev));
    }
    TEST_ASSERT_EQUAL_UINT32(3u, svc.ev_dropped);
    for (i = 0; i < SEC_EVQ_DEPTH; i++) {
        TEST_ASSERT_EQUAL_HEX16(0x0608u, take_event());
    }
    TEST_ASSERT_EQUAL_HEX16(0u, take_event());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_executes_and_advances);
    RUN_TEST(test_wrong_key_rejected_at_auth);
    RUN_TEST(test_replay_rejected);
    RUN_TEST(test_far_future_rejected_by_window);
    RUN_TEST(test_burst_gives_bounded_events);
    RUN_TEST(test_sustained_flood_bounded);
    RUN_TEST(test_check_order);
    RUN_TEST(test_plain_tc_rejected);
    RUN_TEST(test_counter_not_advanced_by_semantic_reject);
    RUN_TEST(test_spi_rules);
    RUN_TEST(test_trusted_release);
    RUN_TEST(test_rekey_load_then_activate);
    RUN_TEST(test_rekey_bad_wrap_fails_execution);
    RUN_TEST(test_210_acceptance_checks);
    RUN_TEST(test_recovery_reset_counter);
    RUN_TEST(test_recovery_activate_spare);
    RUN_TEST(test_first_contact_after_factory);
    RUN_TEST(test_event_queue_bounded);
    return UNITY_END();
}
