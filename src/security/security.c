/* Telecommand security in the integrated application. See security.h. */
#include "security.h"

#include <rtems.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keystore.h"
#include "pus.h"
#include "sec_report.h"
#include "sec_service.h"
#include "services.h"
#include "tc_auth.h"
#include "tmtc.h"

static keystore_t    g_ks;
static tc_auth_t     g_auth;
static sec_service_t g_svc;
static sec_report_t  g_rep;
static tm_source_t   sec_src = { APID_SEC, 0 };
static uint8_t       g_flags;

/* ---- Non volatile key store ---------------------------------------------------- */

#ifdef __rtems__
/* SIS has no non volatile memory: a RAM image that lasts one run. On a
 * flight unit this is the key store partition of the NV memory, written
 * with read-back verification. */
static uint8_t nv_image[KS_IMAGE_LEN];
static int sec_nv_load(uint8_t *img, size_t len)
{
    (void)memcpy(img, nv_image, len);
    return 0;
}
static int sec_nv_store(const uint8_t *img, size_t len)
{
    (void)memcpy(nv_image, img, len);
    return 0;
}
#else
/* Host simulation: a file next to the FDIR NV file, which survives the
 * process "reset" (SEC_NV_FILE, else $FDIR_NV_FILE.sec, else sec_nv.bin). */
static const char *sec_nv_path(void)
{
    static char path[512];
    const char *p = getenv("SEC_NV_FILE");
    const char *f = getenv("FDIR_NV_FILE");

    if (p != NULL) {
        return p;
    }
    if (f != NULL && strlen(f) + 5u < sizeof(path)) {
        (void)snprintf(path, sizeof(path), "%s.sec", f);
        return path;
    }
    return "sec_nv.bin";
}
static int sec_nv_load(uint8_t *img, size_t len)
{
    FILE *f = fopen(sec_nv_path(), "rb");
    size_t got = 0u;

    (void)memset(img, 0, len);
    if (f != NULL) {
        got = fread(img, 1, len, f);
        (void)fclose(f);
    }
    return got == len ? 0 : -1;
}
static int sec_nv_store(const uint8_t *img, size_t len)
{
    FILE *f = fopen(sec_nv_path(), "wb");
    int ok;

    if (f == NULL) {
        return -1;
    }
    ok = fwrite(img, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    return ok ? 0 : -1;
}
#endif

static void sec_persist(const keystore_t *ks, void *arg)
{
    uint8_t img[KS_IMAGE_LEN];
    size_t n = ks_encode(ks, img, sizeof(img));

    (void)arg;
    if (n == 0u || sec_nv_store(img, n) != 0) {
        g_flags |= SEC_FL_NV_FAIL;
    } else {
        g_flags &= (uint8_t)~SEC_FL_NV_FAIL;
    }
    (void)memset(img, 0, sizeof(img));
}

#ifdef FDIR_TEST_BUILD
/* ---- Service 211: security fault injection (test builds only, like 200) --------
 *   TC(211,1) slot u8, copy0 u32, copy1 u32, copy2 u32   set counter copies
 *   TC(211,2) slot u8, copies u8, octet u8, mask u8      flip key bits in the
 *             copies whose bit is set (bit 0: the copy used for MACs)
 * Stands for memory upsets, which the host simulation cannot produce. */
#define SVC_SEC_INJECT 211u

static uint32_t inj_get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static pus_fail_t inject_check(const pus_tc_t *tc, void *arg)
{
    (void)arg;
    if (tc->subtype == 1u) {
        return (tc->app_len == 13u && (tc->app_data[0] == 1u || tc->app_data[0] == 2u))
                   ? PUS_OK : PUS_FAIL_APP_DATA;
    }
    if (tc->subtype == 2u) {
        return (tc->app_len == 4u && (tc->app_data[0] == 1u || tc->app_data[0] == 2u) &&
                tc->app_data[1] != 0u && tc->app_data[1] < 8u &&
                tc->app_data[2] < KS_KEY_LEN)
                   ? PUS_OK : PUS_FAIL_APP_DATA;
    }
    return PUS_FAIL_SUBTYPE;
}

static pus_fail_t inject_exec(const pus_tc_t *tc, struct tc_ctx *ctx, void *arg)
{
    ks_slot_t *k = &g_ks.slot[tc->app_data[0]];
    (void)ctx; (void)arg;
    if (tc->subtype == 1u) {
        k->ctr[0] = inj_get32(&tc->app_data[1]);
        k->ctr[1] = inj_get32(&tc->app_data[5]);
        k->ctr[2] = inj_get32(&tc->app_data[9]);
    } else {
        uint8_t copies = tc->app_data[1], at = tc->app_data[2], m = tc->app_data[3];
        if ((copies & 1u) != 0u) { k->key[at] ^= m; }
        if ((copies & 2u) != 0u) { k->key_copy[0][at] ^= m; }
        if ((copies & 4u) != 0u) { k->key_copy[1][at] ^= m; }
    }
    sec_persist(&g_ks, NULL);
    return PUS_OK;
}
#endif

/* ---- Hooks ---------------------------------------------------------------------- */

int security_init(tc_ctx_t *ctx)
{
    uint8_t img[KS_IMAGE_LEN];

    if (sec_nv_load(img, sizeof(img)) != 0 || ks_decode(&g_ks, img, sizeof(img)) != 0) {
        static const uint8_t ev[2] = { (uint8_t)(SEC_EVT_FACTORY_STATE >> 8),
                                       (uint8_t)SEC_EVT_FACTORY_STATE };
        ks_factory(&g_ks);
        g_flags |= SEC_FL_FACTORY;
        tc_auth_init(&g_auth, &g_ks, sec_persist, NULL);
        sec_service_init(&g_svc, &g_auth);
        sec_service_queue(&g_svc, 2u, ev, sizeof(ev));
        sec_persist(&g_ks, NULL);
    } else {
        tc_auth_init(&g_auth, &g_ks, sec_persist, NULL);
        sec_service_init(&g_svc, &g_auth);
    }
    (void)memset(img, 0, sizeof(img));
    sec_report_init(&g_rep);

    if (sec_service_register(&g_svc, ctx) != 0) {
        return -1;
    }
#ifdef FDIR_TEST_BUILD
    if (tc_register_service(ctx, SVC_SEC_INJECT, inject_check, inject_exec,
                            NULL) != 0) {
        return -1;
    }
#endif
    ctx->security = tc_auth_hook(&g_auth);   /* from now on: authenticated */
    printf("Security: TC authentication ON (HMAC-SHA-256/128), active key "
           "slot %u, key store %s\n", (unsigned)g_ks.active,
           (g_flags & SEC_FL_FACTORY)
               ? "FACTORY (NV blank): waiting for first contact, TC(210,10)"
               : "from NV");
    return 0;
}

void security_trusted(int on)
{
    g_auth.trusted = on ? 1 : 0;      /* caller holds the tc lock */
}

/* ---- Security housekeeping (TM_HK task) ------------------------------------------ */

static void emit_tm(uint8_t service, uint8_t subtype, const uint8_t *d, size_t n)
{
    uint8_t pkt[PUS_MAX_PACKET];
    int len = pus_tm_build(&sec_src, service, subtype, 0, tmtc_now(), d, n,
                           pkt, sizeof(pkt));
    if (len > 0) {
        (void)tmtc_tm_sink(pkt, (size_t)len, NULL);
    }
}

static void emit_event(uint8_t severity, const uint8_t *aux, size_t len, void *arg)
{
    (void)arg;
    emit_tm(5u, severity, aux, len);
}

static uint16_t sec_sat16(uint32_t v) { return v > 0xFFFFu ? 0xFFFFu : (uint16_t)v; }
static uint8_t  sec_sat8(uint32_t v)  { return v > 0xFFu ? 0xFFu : (uint8_t)v; }

static void sec_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/* Key state, never key material: states, check values, counters. */
static void build_sid3(uint8_t d[HK_SID3_LEN])
{
    uint32_t v;
    size_t c;
    uint8_t s;

    d[0] = HK_SID_SECURITY;
    d[1] = g_ks.active;
    for (s = 1u; s < KS_SLOTS; s++) {
        d[1u + s] = g_ks.slot[s].state;
        sec_put32(&d[4u + 4u * (s - 1u)], g_ks.slot[s].kcv[0]);
        if (ks_counter(&g_ks, s, &v) > 0) {
            g_auth.vote_repairs |= (uint8_t)(1u << s);
        }
        sec_put32(&d[12u + 4u * (s - 1u)], v);
    }
    if (ks_challenge(&g_ks, &v) > 0) {
        g_auth.vote_repairs |= 1u;
    }
    sec_put32(&d[20], v);
    for (c = 0; c < (size_t)SEC_REJ_CLASSES; c++) {
        pus_put_u16(&d[24u + 2u * c], sec_sat16(g_auth.rej_total[c]));
    }
    d[36] = sec_sat8(g_ks.scrub_errors);
    d[37] = sec_sat8(g_ks.scrub_repairs);
    d[38] = (uint8_t)(g_flags | (g_svc.ev_dropped > 0u ? SEC_FL_EV_DROPPED : 0u) |
                      (ks_counter_untrusted(&g_ks, 1u) ? SEC_FL_CTR1_UNTRUSTED : 0u) |
                      (ks_counter_untrusted(&g_ks, 2u) ? SEC_FL_CTR2_UNTRUSTED : 0u));
    pus_put_u16(&d[39], sec_sat16(g_auth.auth_accepted));
}

void security_hk_cycle(void)
{
    static uint32_t cycle;
    uint32_t totals[SEC_REJ_CLASSES];
    uint8_t codes[SEC_REJ_CLASSES];
    uint8_t ev[SEC_EVQ_DEPTH][SEC_EV_MAX];
    uint8_t ev_sev[SEC_EVQ_DEPTH];
    size_t ev_len[SEC_EVQ_DEPTH];
    uint8_t sid3[HK_SID3_LEN];
    size_t n_ev = 0u, i;
    int send_hk = (cycle % 2u) == 0u;           /* 1 Hz in every mode */
    uint32_t now_ms =
        (uint32_t)(rtems_clock_get_uptime_nanoseconds() / 1000000u);

    tmtc_lock();
    if (send_hk) {
        uint32_t repairs = g_ks.scrub_repairs;
        unsigned lost = ks_scrub(&g_ks);
        if (lost != 0u) {
            uint8_t a[5];
            pus_put_u16(&a[0], SEC_EVT_KEY_CORRUPT);
            a[2] = (uint8_t)lost;
            pus_put_u16(&a[3], sec_sat16(g_ks.scrub_errors));
            sec_service_queue(&g_svc, 4u, a, sizeof(a));
        }
        if (g_ks.scrub_repairs != repairs) {
            uint8_t a[4];
            pus_put_u16(&a[0], SEC_EVT_KEY_REPAIRED);
            pus_put_u16(&a[2], sec_sat16(g_ks.scrub_repairs));
            sec_service_queue(&g_svc, 1u, a, sizeof(a));
        }
        build_sid3(sid3);
        if (lost != 0u || g_ks.scrub_repairs != repairs) {
            sec_persist(&g_ks, NULL);
        }
    }
    if (g_auth.vote_repairs != 0u) {
        uint8_t a[3];
        pus_put_u16(&a[0], SEC_EVT_COUNTER_VOTE);
        a[2] = g_auth.vote_repairs;
        g_auth.vote_repairs = 0u;
        sec_service_queue(&g_svc, 3u, a, sizeof(a));
        sec_persist(&g_ks, NULL);
    }
    (void)memcpy(totals, g_auth.rej_total, sizeof(totals));
    (void)memcpy(codes, g_auth.rej_last_code, sizeof(codes));
    while (n_ev < SEC_EVQ_DEPTH &&
           (ev_len[n_ev] = sec_service_take(&g_svc, &ev_sev[n_ev], ev[n_ev])) > 0u) {
        n_ev++;
    }
    tmtc_unlock();

    /* Telemetry is built outside the tc lock. */
    for (i = 0; i < n_ev; i++) {
        emit_event(ev_sev[i], ev[i], ev_len[i], NULL);
    }
    sec_report_cycle(&g_rep, totals, codes, now_ms, emit_event, NULL);
    if (send_hk) {
        emit_tm(3u, 25u, sid3, sizeof(sid3));
    }
    cycle++;
}
