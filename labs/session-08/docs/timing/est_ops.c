/*
 * Session 8 - instruction count estimate of the security operations, for
 * the timing budget BEFORE target measurement (security-report.md S4).
 *
 * Run under valgrind --tool=callgrind (estimate.sh). Each op_* function
 * runs its operation REPS times; work_loop() is the Lab 3.2 bounded work
 * loop (lab5.c do_bounded_work), whose SIS time is known from Session 4:
 * ACS_WORK = 43000 iterations took 18.957 ms. The ratio of instruction
 * counts, times that calibration, estimates the LEON3 time. It is an
 * estimate (x86-64 instruction counts are not SPARC ones); the SIS run
 * (app/run-sis-timing.sh) replaces it.
 */
#include <stdint.h>
#include <string.h>

#include "crc16.h"
#include "keystore.h"
#include "pus.h"
#include "tc_auth.h"
#include "tc_handler.h"

#define REPS 50u
static keystore_t ks;
static tc_auth_t auth;
static tc_ctx_t ctx;
static uint8_t fr[REPS][PUS_MAX_PACKET];
static size_t ln[REPS];

static int sink(const uint8_t *p, size_t n, void *a) { (void)p; (void)n; (void)a; return 0; }
static obt_t now(void) { obt_t t = { 0u, 0u }; return t; }
static pus_fail_t chk(const pus_tc_t *t, void *a) { (void)t; (void)a; return PUS_OK; }
static pus_fail_t exe(const pus_tc_t *t, struct tc_ctx *c, void *a) { (void)t; (void)c; (void)a; return PUS_OK; }

__attribute__((noinline)) void work_loop(uint32_t iterations);
__attribute__((noinline)) void work_loop(uint32_t iterations)
{
    volatile uint32_t i;
    volatile uint32_t dummy = 0;
    for (i = 0; i < iterations; i++) { dummy += i; }
}

static void build(int secure, int bad)
{
    uint32_t k;
    for (k = 0; k < REPS; k++) {
        uint8_t app[PUS_TC_MAX_DATA];
        size_t dl = PUS_TC_MAX_DATA - (secure ? SEC_TRAILER_LEN : 0u);
        uint16_t crc;
        memset(app, 0x5A, sizeof(app));
        if (secure) {
            uint32_t s = k + 1u + (bad ? 1000u : 0u);
            app[dl] = 1u; app[dl + 1] = (uint8_t)(s >> 24); app[dl + 2] = (uint8_t)(s >> 16);
            app[dl + 3] = (uint8_t)(s >> 8); app[dl + 4] = (uint8_t)s;
        }
        ln[k] = (size_t)pus_tc_build(0x010u, 0, 0x9u, 99u, 1u, 1u, app, PUS_TC_MAX_DATA, fr[k], PUS_MAX_PACKET);
        if (secure) {
            (void)ks_mac(&ks, 1u, fr[k], 11u + dl + 5u, &fr[k][11u + dl + 5u]);
            if (bad) { fr[k][ln[k] - 3u] ^= 1u; }
            crc = crc16_ccitt(fr[k], ln[k] - 2u);
            fr[k][ln[k] - 2u] = (uint8_t)(crc >> 8); fr[k][ln[k] - 1u] = (uint8_t)crc;
        }
    }
}

__attribute__((noinline)) void op_plain(void);
void op_plain(void) { uint32_t k; for (k = 0; k < REPS; k++) tc_handle(&ctx, fr[k], ln[k]); }
__attribute__((noinline)) void op_accept(void);
void op_accept(void) { uint32_t k; for (k = 0; k < REPS; k++) tc_handle(&ctx, fr[k], ln[k]); }
__attribute__((noinline)) void op_reject(void);
void op_reject(void) { uint32_t k; for (k = 0; k < REPS; k++) tc_handle(&ctx, fr[k], ln[k]); }
__attribute__((noinline)) void op_scrub(void);
void op_scrub(void) { uint32_t k; for (k = 0; k < REPS; k++) (void)ks_scrub(&ks); }
__attribute__((noinline)) void op_load(void);
void op_load(void)
{
    uint8_t n[KS_NONCE_LEN] = { 0 }, w[KS_WRAPPED_LEN] = { 0 };
    uint32_t k;
    for (k = 0; k < REPS; k++) (void)ks_load(&ks, 2u, n, w);
}

int main(void)
{
    ks_factory(&ks);
    ks_counter_floor(&ks, 1u, 0u);
    ks.slot[2] = ks.slot[1];
    ks.slot[2].state = KS_LOADED;
    tc_auth_init(&auth, &ks, NULL, NULL);
    tc_init(&ctx, 0x010u, sink, NULL, now);
    (void)tc_register_service(&ctx, 99u, chk, exe, NULL);
    work_loop(43000u);
    build(0, 0); op_plain();
    ctx.security = tc_auth_hook(&auth);
    build(1, 0); op_accept();
    build(1, 1); op_reject();
    op_scrub();
    op_load();
    return (int)(ctx.accepted & 0u);
}
