/*
 * Session 8 - cost of the security functions, for the timing budget
 * (docs/security-report.md, Section 4).
 *
 * Runs each operation N times back to back in the Init task, interrupts on,
 * and prints the mean and the largest single run in microseconds. On SIS
 * this is simulated LEON3 time; on the host it is only a comparison point.
 *
 *   accept    tc_handle() of a secured TC, 120 octets (the largest packet),
 *             through verify, the service check, commit, exec and TM(1,x)
 *   reject    tc_handle() of the same TC with a wrong MAC: the cost an
 *             attacker without the key can impose per packet
 *   plain     tc_handle() of a 120 octet TC with no security (Session 4)
 *   mac       ks_mac() over 99 octets alone (the authenticated part)
 *   scrub     ks_scrub() of two loaded slots (runs at 1 Hz in TM_HK)
 *   load      ks_load(): AES-128-CCM unwrap of a 32 octet key + KCV
 */
#include <rtems.h>
#include <stdio.h>
#include <string.h>

#include "crc16.h"
#include "keystore.h"
#include "pus.h"
#include "tc_auth.h"
#include "tc_handler.h"

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER
#define CONFIGURE_MAXIMUM_TASKS 2
#define CONFIGURE_MICROSECONDS_PER_TICK 10000
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INIT_TASK_STACK_SIZE (16 * 1024)
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT
#include <rtems/confdefs.h>

#define N 200u
#define SVC 99u

static keystore_t ks;
static tc_auth_t auth;
static tc_ctx_t ctx;
static volatile uint32_t sink_count;

static int sink(const uint8_t *p, size_t n, void *arg)
{
    (void)p; (void)n; (void)arg;
    sink_count++;
    return 0;
}
static obt_t now(void) { return obt_from_ns(rtems_clock_get_uptime_nanoseconds()); }
static pus_fail_t chk(const pus_tc_t *tc, void *arg) { (void)tc; (void)arg; return PUS_OK; }
static pus_fail_t exe(const pus_tc_t *tc, struct tc_ctx *c, void *arg)
{
    (void)tc; (void)c; (void)arg;
    return PUS_OK;
}

static size_t make_tc(uint8_t *out, int secure, uint32_t seq)
{
    uint8_t app[PUS_TC_MAX_DATA];
    size_t data_len = PUS_TC_MAX_DATA - (secure ? SEC_TRAILER_LEN : 0u);
    size_t n, t;
    uint16_t crc;

    memset(app, 0x5A, sizeof(app));
    if (secure) {
        app[data_len] = ks.active;
        app[data_len + 1u] = (uint8_t)(seq >> 24); app[data_len + 2u] = (uint8_t)(seq >> 16);
        app[data_len + 3u] = (uint8_t)(seq >> 8);  app[data_len + 4u] = (uint8_t)seq;
    }
    n = (size_t)pus_tc_build(0x010u, 0, 0x9u, SVC, 1u, 1u, app, PUS_TC_MAX_DATA,
                             out, PUS_MAX_PACKET);
    if (secure) {
        t = 11u + data_len;
        (void)ks_mac(&ks, ks.active, out, t + 5u, &out[t + 5u]);
        crc = crc16_ccitt(out, n - 2u);
        out[n - 2u] = (uint8_t)(crc >> 8);
        out[n - 1u] = (uint8_t)crc;
    }
    return n;
}

typedef struct { const char *name; uint64_t total, max; } result_t;

static void report(const result_t *r)
{
    uint64_t mean_ns = r->total / N;
    printf("  %-8s mean %7lu.%01lu us   max %7lu.%01lu us   (N=%u)\n", r->name,
           (unsigned long)(mean_ns / 1000u), (unsigned long)((mean_ns % 1000u) / 100u),
           (unsigned long)(r->max / 1000u), (unsigned long)((r->max % 1000u) / 100u),
           (unsigned)N);
}

#define MEASURE(res, stmt) do {                                        \
        uint32_t i_;                                                   \
        (res).total = 0u; (res).max = 0u;                              \
        for (i_ = 0; i_ < N; i_++) {                                   \
            uint64_t t0_ = rtems_clock_get_uptime_nanoseconds();       \
            stmt;                                                      \
            uint64_t d_ = rtems_clock_get_uptime_nanoseconds() - t0_;  \
            (res).total += d_;                                         \
            if (d_ > (res).max) { (res).max = d_; }                    \
        }                                                              \
    } while (0)

rtems_task Init(rtems_task_argument arg);
rtems_task Init(rtems_task_argument arg)
{
    static uint8_t frames[N][PUS_MAX_PACKET];
    static size_t lens[N];
    uint8_t mac[KS_MAC_LEN];
    uint8_t nonce[KS_NONCE_LEN] = { 0 };
    uint8_t wrapped[KS_WRAPPED_LEN] = { 0 };
    result_t r;
    uint32_t i;
    uint32_t accepted_before;

    (void)arg;
    ks_factory(&ks);
    ks_counter_floor(&ks, 1u, 0u);
    ks.slot[2] = ks.slot[1];
    ks.slot[2].state = KS_LOADED;
    tc_auth_init(&auth, &ks, NULL, NULL);
    tc_init(&ctx, 0x010u, sink, NULL, now);
    (void)tc_register_service(&ctx, SVC, chk, exe, NULL);

    printf("\nSession 8 security timing (%s)\n",
#ifdef __rtems__
           "RTEMS 6, LEON3"
#else
           "HOST SIMULATION - not representative of the target"
#endif
    );

    ctx.security = NULL;
    for (i = 0; i < N; i++) { lens[i] = make_tc(frames[i], 0, 0); }
    r.name = "plain";
    { uint32_t k = 0; MEASURE(r, tc_handle(&ctx, frames[k], lens[k]); k++); }
    report(&r);

    ctx.security = tc_auth_hook(&auth);
    for (i = 0; i < N; i++) { lens[i] = make_tc(frames[i], 1, i + 1u); }
    accepted_before = ctx.accepted;
    r.name = "accept";
    { uint32_t k = 0; MEASURE(r, tc_handle(&ctx, frames[k], lens[k]); k++); }
    report(&r);
    printf("           (%lu of %u accepted)\n",
           (unsigned long)(ctx.accepted - accepted_before), (unsigned)N);

    for (i = 0; i < N; i++) {
        uint16_t crc;
        frames[i][lens[i] - 3u] ^= 0x01u;          /* last MAC octet */
        crc = crc16_ccitt(frames[i], lens[i] - 2u);
        frames[i][lens[i] - 2u] = (uint8_t)(crc >> 8);
        frames[i][lens[i] - 1u] = (uint8_t)crc;
    }
    r.name = "reject";
    { uint32_t k = 0; MEASURE(r, tc_handle(&ctx, frames[k], lens[k]); k++); }
    report(&r);
    printf("           (%lu authentication rejections)\n",
           (unsigned long)auth.rej_total[SEC_REJ_AUTH]);

    r.name = "mac";
    MEASURE(r, (void)ks_mac(&ks, 1u, frames[0], 99u, mac));
    report(&r);

    r.name = "scrub";
    MEASURE(r, (void)ks_scrub(&ks));
    report(&r);

    ks.active = 1u;
    r.name = "load";            /* fails verification: same work as success */
    MEASURE(r, (void)ks_load(&ks, 2u, nonce, wrapped));
    report(&r);

    printf("Timing finished.\n");
    rtems_shutdown_executive(0);
}
