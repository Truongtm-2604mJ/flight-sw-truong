/*
 * Session 5 application - telecommand services.
 *
 * Each service is a check (acceptance: structure and ranges only, no
 * side effect) and an exec, as in Session 4. All of them run under the
 * tc lock.
 *
 *   TC(8,1)   perform function. FUNC_SET_MODE: u8 func, u8 mode.
 *   TC(11,1)  enable the time based schedule        (no data)
 *   TC(11,2)  disable it                            (no data)
 *   TC(11,3)  reset it (delete every activity)      (no data)
 *   TC(11,4)  insert one activity: u8 N=1, u32 release time (onboard s),
 *             then the complete telecommand packet to release
 *   TC(12,1)  enable parameter monitoring:  u8 N, N x u16 parameter ID
 *   TC(12,2)  disable parameter monitoring: same
 *   TC(19,8)  enable autonomous FDIR actions        (no data)
 *   TC(19,9)  disable (inhibit) them: u16 duration in s, 1..3600
 *   TC(200,x) fault injection, test builds only (see finj.h)
 */
#include "services.h"

#include "fdir.h"
#include "fdir_app.h"
#include "finj.h"
#include "pus.h"

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* ---- Service 8: function management -------------------------------------------- */

static pus_fail_t svc8_check(const pus_tc_t *tc, void *arg)
{
    (void)arg;
    if (tc->subtype != 1u) {
        return PUS_FAIL_SUBTYPE;
    }
    if (tc->app_len != 2u || tc->app_data[0] != FUNC_SET_MODE ||
        tc->app_data[1] > (uint8_t)MODE_SAFE) {
        return PUS_FAIL_APP_DATA;
    }
    return PUS_OK;
}

static pus_fail_t svc8_exec(const pus_tc_t *tc, tc_ctx_t *ctx, void *arg)
{
    (void)ctx; (void)arg;
    (void)fdir_mode_request((vmode_t)tc->app_data[1], CAUSE_GROUND, 0);
    return PUS_OK;      /* already in that mode is not a failure */
}

/* ---- Service 11: time based schedule ---------------------------------------------- */

static pus_fail_t svc11_check(const pus_tc_t *tc, void *arg)
{
    (void)arg;
    switch (tc->subtype) {
    case 1u: case 2u: case 3u:
        return tc->app_len == 0u ? PUS_OK : PUS_FAIL_APP_DATA;
    case 4u:
        if (tc->app_len < 1u + 4u + PUS_TC_MIN_LEN ||
            tc->app_len - 5u > SCHED_MAX_TC || tc->app_data[0] != 1u) {
            return PUS_FAIL_APP_DATA;
        }
        return PUS_OK;
    default:
        return PUS_FAIL_SUBTYPE;
    }
}

static pus_fail_t svc11_exec(const pus_tc_t *tc, tc_ctx_t *ctx, void *arg)
{
    sched_t *s = fdir_schedule();
    (void)ctx; (void)arg;
    switch (tc->subtype) {
    case 1u: sched_set_enabled(s, 1); return PUS_OK;
    case 2u: sched_set_enabled(s, 0); return PUS_OK;
    case 3u: sched_reset(s);          return PUS_OK;
    default:
        return sched_insert(s, get_u32(&tc->app_data[1]), &tc->app_data[5],
                            tc->app_len - 5u) == 0 ? PUS_OK
                                                   : PUS_FAIL_EXECUTION;
    }
}

/* ---- Service 12: parameter monitoring ---------------------------------------------- */

static pus_fail_t svc12_check(const pus_tc_t *tc, void *arg)
{
    size_t i, n;
    (void)arg;
    if (tc->subtype != 1u && tc->subtype != 2u) {
        return PUS_FAIL_SUBTYPE;
    }
    if (tc->app_len < 3u) {
        return PUS_FAIL_APP_DATA;
    }
    n = tc->app_data[0];
    if (n == 0u || tc->app_len != 1u + 2u * n) {
        return PUS_FAIL_APP_DATA;
    }
    for (i = 0; i < n; i++) {
        if (!fdir_param_known(pus_get_u16(&tc->app_data[1u + 2u * i]))) {
            return PUS_FAIL_APP_DATA;
        }
    }
    return PUS_OK;
}

static pus_fail_t svc12_exec(const pus_tc_t *tc, tc_ctx_t *ctx, void *arg)
{
    size_t i;
    (void)ctx; (void)arg;
    for (i = 0; i < tc->app_data[0]; i++) {
        fdir_monitor_enable(pus_get_u16(&tc->app_data[1u + 2u * i]),
                            tc->subtype == 1u);
    }
    return PUS_OK;
}

/* ---- Service 19: event action (here: all autonomous FDIR actions) -------------------- */

static pus_fail_t svc19_check(const pus_tc_t *tc, void *arg)
{
    (void)arg;
    if (tc->subtype == 8u) {
        return tc->app_len == 0u ? PUS_OK : PUS_FAIL_APP_DATA;
    }
    if (tc->subtype == 9u) {
        uint16_t d;
        if (tc->app_len != 2u) {
            return PUS_FAIL_APP_DATA;
        }
        d = pus_get_u16(tc->app_data);
        return (d >= 1u && d <= MODE_INHIBIT_MAX_S) ? PUS_OK
                                                     : PUS_FAIL_APP_DATA;
    }
    return PUS_FAIL_SUBTYPE;
}

static pus_fail_t svc19_exec(const pus_tc_t *tc, tc_ctx_t *ctx, void *arg)
{
    (void)ctx; (void)arg;
    if (tc->subtype == 8u) {
        return fdir_set_inhibit(0, 0) == 0 ? PUS_OK : PUS_FAIL_EXECUTION;
    }
    return fdir_set_inhibit(1, pus_get_u16(tc->app_data)) == 0
           ? PUS_OK : PUS_FAIL_EXECUTION;
}

/* ---- Service 200: fault injection (test builds only) --------------------------------- */

#ifdef FDIR_TEST_BUILD
static pus_fail_t svc200_check(const pus_tc_t *tc, void *arg)
{
    (void)arg;
    switch (tc->subtype) {
    case 1u:   /* u16 param, i32 value, u16 samples (0 = sustained) */
        if (tc->app_len != 8u || !fdir_param_known(pus_get_u16(tc->app_data))) {
            return PUS_FAIL_APP_DATA;
        }
        return PUS_OK;
    case 2u:   /* u8 task, u8 times */
        if (tc->app_len != 2u || tc->app_data[0] >= NUM_SUPERVISED ||
            tc->app_data[1] == 0u) {
            return PUS_FAIL_APP_DATA;
        }
        return PUS_OK;
    case 3u: case 4u:
        return tc->app_len == 0u ? PUS_OK : PUS_FAIL_APP_DATA;
    default:
        return PUS_FAIL_SUBTYPE;
    }
}

static pus_fail_t svc200_exec(const pus_tc_t *tc, tc_ctx_t *ctx, void *arg)
{
    const uint8_t *d = tc->app_data;
    (void)ctx; (void)arg;
    switch (tc->subtype) {
    case 1u:
        finj_param_override(pus_get_u16(d), (int32_t)get_u32(&d[2]),
                            pus_get_u16(&d[6]));
        break;
    case 2u: finj_task_hang(d[0], d[1]); break;
    case 3u: finj_supervisor_hang();     break;
    default: finj_clear();               break;
    }
    return PUS_OK;
}
#endif

int services_register(tc_ctx_t *ctx)
{
    if (tc_register_service(ctx, SVC_FUNCTION, svc8_check, svc8_exec, NULL) ||
        tc_register_service(ctx, SVC_SCHEDULE, svc11_check, svc11_exec, NULL) ||
        tc_register_service(ctx, SVC_MONITORING, svc12_check, svc12_exec, NULL) ||
        tc_register_service(ctx, SVC_EVENT_ACTION, svc19_check, svc19_exec, NULL)) {
        return -1;
    }
#ifdef FDIR_TEST_BUILD
    if (tc_register_service(ctx, SVC_FAULT_INJECT, svc200_check, svc200_exec,
                            NULL)) {
        return -1;
    }
#endif
    return 0;
}
