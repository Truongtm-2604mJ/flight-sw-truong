/* Unit suite: liveness supervision, watchdog model and NV record
 * (supervisor.c, watchdog.c, nvstate.c).
 *
 * Ported to Unity from session-05/lab-5.2-liveness/tests/test_liveness.c by tests/support/port_to_unity.py.
 */
/*
 * Lab 5.2 - Supervisor, watchdog and NV record tests.
 * Build and run: make test
 */
#include "supervisor.h"
#include "watchdog.h"
#include "nvstate.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

/* ---- Fake operating system around the supervisor ------------------------ */

#define LOG_MAX 64
static uint16_t ev_id[LOG_MAX];
static size_t   ev_task[LOG_MAX];
static int      ev_n;
static int      restarts[SUP_MAX_TASKS];
static int      escalated_task = -1;
static uint8_t  escalated_to;
static int      inhibited;

static int fake_restart(void *arg, size_t idx, uint32_t os_id)
{
    (void)arg; (void)os_id;
    restarts[idx]++;
    return 0;
}
static void fake_escalate(void *arg, size_t idx, uint8_t esc)
{
    (void)arg;
    escalated_task = (int)idx;
    escalated_to = esc;
}
static void fake_event(void *arg, uint16_t id, size_t idx, uint32_t value)
{
    (void)arg; (void)value;
    if (ev_n < LOG_MAX) { ev_id[ev_n] = id; ev_task[ev_n] = idx; }
    ev_n++;
}
static int fake_inhibited(void *arg) { (void)arg; return inhibited; }

static const sup_ops_t ops = { fake_restart, fake_escalate, fake_event,
                               fake_inhibited };

static int count_events(uint16_t id)
{
    int i, n = 0;
    for (i = 0; i < ev_n && i < LOG_MAX; i++) {
        if (ev_id[i] == id) { n++; }
    }
    return n;
}

/* ACS 100 ms, HK 500 ms, LOG 1000 ms, checked every 2000 ms, three
 * restarts allowed per 300 s window. */
static supervisor_t sup;
static uint32_t now;

static void setup(void)
{
    memset(restarts, 0, sizeof(restarts));
    ev_n = 0; escalated_task = -1; inhibited = 0; now = 0;
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(sup_init(&sup, 2000, 300000, 3, &ops, NULL)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(sup_register(&sup, "ACS", 100, 11, 2)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(sup_register(&sup, "HK", 500, 12, 2)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(sup_register(&sup, "LOG", 1000, 13, 1)));
}

/* Advance one supervision interval with the given tasks alive. */
static void cycle(int acs, int hk, int log)
{
    int k;
    for (k = 0; k < 20; k++) { if (acs) sup_heartbeat(&sup, 0); }
    for (k = 0; k < 4; k++)  { if (hk)  sup_heartbeat(&sup, 1); }
    for (k = 0; k < 2; k++)  { if (log) sup_heartbeat(&sup, 2); }
    now += 2000;
    sup_check(&sup, now);
}

static void test_registration_rules(void)
{
    supervisor_t s;
    TEST_ASSERT_EQUAL_INT64((int64_t)(-1), (int64_t)(sup_init(&s, 2000, 300000, 0, &ops, NULL)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(-1), (int64_t)(sup_init(&s, 2000, 300000, SUP_MAX_RESTARTS + 1, &ops, NULL)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(sup_init(&s, 2000, 300000, 3, &ops, NULL)));
    /* A task as slow as the supervisor could miss a whole interval
     * while healthy: refused. */
    TEST_ASSERT_EQUAL_INT64((int64_t)(-1), (int64_t)(sup_register(&s, "SLOW", 2000, 1, 0)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(sup_register(&s, "OK", 1999, 1, 0)));
}

static void test_healthy_tasks_raise_nothing(void)
{
    int i;
    setup();
    for (i = 0; i < 1000; i++) { cycle(1, 1, 1); }  /* 2000 s */
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(ev_n));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(restarts[0] + restarts[1] + restarts[2]));
}

static void test_single_missed_heartbeat_restarts_only_that_task(void)
{
    setup();
    cycle(1, 1, 1);
    cycle(1, 1, 0);                      /* LOG silent for one interval */
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(count_events(SUP_EVT_HEARTBEAT_MISSED)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(count_events(SUP_EVT_TASK_RESTARTED)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(ev_task[0]));             /* isolated to LOG */
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(restarts[2]));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(restarts[0] + restarts[1]));
    cycle(1, 1, 1);                      /* recovered */
    cycle(1, 1, 1);
    TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(ev_n));
    TEST_ASSERT_EQUAL_INT64((int64_t)(SUP_TASK_OK), (int64_t)(sup.tasks[2].status));
}

static void test_repeated_failure_escalates_after_three_restarts(void)
{
    int i;
    setup();
    cycle(1, 1, 1);
    for (i = 0; i < 5; i++) { cycle(0, 1, 1); }   /* ACS keeps failing */

    TEST_ASSERT_EQUAL_INT64((int64_t)(3), (int64_t)(restarts[0]));                     /* bounded           */
    TEST_ASSERT_EQUAL_INT64((int64_t)(3), (int64_t)(count_events(SUP_EVT_TASK_RESTARTED)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(count_events(SUP_EVT_TASK_ESCALATED)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(escalated_task));
    TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(escalated_to));
    TEST_ASSERT_EQUAL_INT64((int64_t)(SUP_TASK_FAILED), (int64_t)(sup.tasks[0].status));
    /* 4 misses reported (3 restarted + 1 escalated); after FAILED the
     * task is no longer checked, so the 5th interval is silent. */
    TEST_ASSERT_EQUAL_INT64((int64_t)(4), (int64_t)(count_events(SUP_EVT_HEARTBEAT_MISSED)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(restarts[1] + restarts[2]));
}

static void test_restarts_outside_the_window_do_not_count(void)
{
    int i;
    setup();
    /* One failure every 200 s: never three inside 300 s. */
    for (i = 0; i < 10; i++) {
        int k;
        cycle(0, 1, 1);
        for (k = 0; k < 99; k++) { cycle(1, 1, 1); }
    }
    TEST_ASSERT_EQUAL_INT64((int64_t)(10), (int64_t)(restarts[0]));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(count_events(SUP_EVT_TASK_ESCALATED)));
    TEST_ASSERT_TRUE(sup_restarts_in_window(&sup, 0, now) <= 2);

    /* Failures 90 s apart: three restarts at 0, 90 and 180 s; the
     * fourth failure (270 s) finds all three inside 300 s and escalates. */
    setup();
    for (i = 0; i < 4; i++) {
        int k;
        cycle(0, 1, 1);
        for (k = 0; k < 44; k++) { cycle(1, 1, 1); }
    }
    TEST_ASSERT_EQUAL_INT64((int64_t)(3), (int64_t)(restarts[0]));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(escalated_task));

    /* Boundary: 100 s apart puts the first restart exactly 300 s before
     * the fourth failure, outside the window, so it restarts again. */
    setup();
    for (i = 0; i < 4; i++) {
        int k;
        cycle(0, 1, 1);
        for (k = 0; k < 49; k++) { cycle(1, 1, 1); }
    }
    TEST_ASSERT_EQUAL_INT64((int64_t)(4), (int64_t)(restarts[0]));
    TEST_ASSERT_EQUAL_INT64((int64_t)(-1), (int64_t)(escalated_task));
}

static void test_window_is_wrap_safe(void)
{
    int i;
    setup();
    now = 0xFFFFFFFFu - 3000u;           /* 32 bit ms counter about to wrap */
    for (i = 0; i < 4; i++) { cycle(1, 0, 1); }
    TEST_ASSERT_EQUAL_INT64((int64_t)(3), (int64_t)(restarts[1]));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(escalated_task));
}

static void test_inhibit_detects_but_does_not_act(void)
{
    int i;
    setup();
    inhibited = 1;
    for (i = 0; i < 10; i++) { cycle(1, 1, 0); }
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(restarts[2]));
    TEST_ASSERT_EQUAL_INT64((int64_t)(-1), (int64_t)(escalated_task));
    /* Reported once per episode, not every interval. */
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(count_events(SUP_EVT_HEARTBEAT_MISSED)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(count_events(SUP_EVT_ACTION_INHIBITED)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(SUP_TASK_MISSING), (int64_t)(sup.tasks[2].status));

    cycle(1, 1, 1);                      /* comes back by itself */
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(count_events(SUP_EVT_TASK_RECOVERED)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(SUP_TASK_OK), (int64_t)(sup.tasks[2].status));

    inhibited = 0;                       /* actions allowed again */
    cycle(1, 1, 0);
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(restarts[2]));
}

static void test_lifting_inhibit_acts_on_a_task_still_silent(void)
{
    setup();
    inhibited = 1;
    cycle(1, 1, 0);
    cycle(1, 1, 0);
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(restarts[2]));
    inhibited = 0;                       /* ground lifts the inhibit */
    cycle(1, 1, 0);                      /* LOG is still silent      */
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(restarts[2]));
    /* The miss was already reported: no duplicate HEARTBEAT_MISSED. */
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(count_events(SUP_EVT_HEARTBEAT_MISSED)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(count_events(SUP_EVT_TASK_RESTARTED)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(SUP_TASK_OK), (int64_t)(sup.tasks[2].status));
}

/* ---- Watchdog ----------------------------------------------------------- */

static void test_watchdog_kicked_never_fires(void)
{
    wdg_t w;
    int i;
    wdg_init(&w, 5000, 1000);
    for (i = 0; i < 10000; i++) {
        TEST_ASSERT_EQUAL_INT64((int64_t)(WDG_NONE), (int64_t)(wdg_tick(&w, 100)));
        if (i % 20 == 19) { wdg_kick(&w); }   /* every 2 s */
    }
}

static void test_watchdog_warns_then_resets(void)
{
    wdg_t w;
    int i, warn_at = -1, reset_at = -1;
    wdg_init(&w, 5000, 1000);
    for (i = 1; i <= 100; i++) {
        wdg_action_t a = wdg_tick(&w, 100);
        if (a == WDG_EARLY_WARNING) { warn_at = i * 100; }
        if (a == WDG_RESET) { reset_at = i * 100; }
    }
    TEST_ASSERT_EQUAL_INT64((int64_t)(5000), (int64_t)(warn_at));
    TEST_ASSERT_EQUAL_INT64((int64_t)(6000), (int64_t)(reset_at));
    TEST_ASSERT_EQUAL_INT64((int64_t)(WDG_EXPIRED), (int64_t)(w.state));
}

static void test_watchdog_late_kick_cannot_cancel_reset(void)
{
    wdg_t w;
    int i, resets = 0;
    wdg_init(&w, 5000, 1000);
    for (i = 0; i < 50; i++) { (void)wdg_tick(&w, 100); }
    TEST_ASSERT_EQUAL_INT64((int64_t)(WDG_WARNED), (int64_t)(w.state));
    wdg_kick(&w);                         /* too late */
    for (i = 0; i < 20; i++) { resets += (wdg_tick(&w, 100) == WDG_RESET); }
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(resets));
}

/* ---- NV record ------------------------------------------------------------ */

static void test_nv_round_trip(void)
{
    nv_record_t a, b;
    uint8_t img[NV_IMAGE_LEN];

    nv_defaults(&a);
    a.boot_count = 7; a.reset_count = 2; a.pending_reset = NV_RESET_WATCHDOG;
    a.escalation_count = 1; a.last_esc_task = 0; a.last_esc_action = 2;
    a.last_esc_boot = 6; a.last_esc_uptime_s = 123456;
    a.last_safe_src = 3; a.last_safe_detail = 0;
    nv_encode(&a, img);
    TEST_ASSERT_EQUAL_INT64((int64_t)('F'), (int64_t)(img[0])); TEST_ASSERT_EQUAL_INT64((int64_t)('R'), (int64_t)(img[3]));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(nv_decode(img, &b)));
    TEST_ASSERT_EQUAL_MEMORY((&b), (&a), (sizeof(a)));
}

static void test_nv_corruption_falls_back_to_cold_start(void)
{
    nv_record_t r;
    uint8_t img[NV_IMAGE_LEN];
    size_t bit;

    nv_defaults(&r);
    r.boot_count = 9;
    nv_encode(&r, img);
    for (bit = 0; bit < NV_IMAGE_LEN * 8u; bit++) {
        uint8_t bad[NV_IMAGE_LEN];
        memcpy(bad, img, sizeof(bad));
        bad[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        TEST_ASSERT_EQUAL_INT64((int64_t)(-1), (int64_t)(nv_decode(bad, &r)));
        TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(r.boot_count));
        TEST_ASSERT_EQUAL_INT64((int64_t)(0xFF), (int64_t)(r.last_esc_task));
    }
    memset(img, 0, sizeof(img));           /* never written */
    TEST_ASSERT_EQUAL_INT64((int64_t)(-1), (int64_t)(nv_decode(img, &r)));
}

static void test_nv_boot_sequence(void)
{
    nv_record_t r;
    uint8_t img[NV_IMAGE_LEN];
    int valid;

    /* Blank memory: cold start, boot 1. */
    memset(img, 0xFF, sizeof(img));
    TEST_ASSERT_EQUAL_INT64((int64_t)(NV_RESET_NONE), (int64_t)(nv_boot(&r, img, &valid)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(valid));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(r.boot_count));

    /* Watchdog records the cause, then the processor resets. */
    r.pending_reset = NV_RESET_WATCHDOG;
    nv_encode(&r, img);
    TEST_ASSERT_EQUAL_INT64((int64_t)(NV_RESET_WATCHDOG), (int64_t)(nv_boot(&r, img, &valid)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(valid));
    TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(r.boot_count));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(r.reset_count));
    TEST_ASSERT_EQUAL_INT64((int64_t)(NV_RESET_WATCHDOG), (int64_t)(r.last_reset_cause));
    TEST_ASSERT_EQUAL_INT64((int64_t)(NV_RESET_NONE), (int64_t)(r.pending_reset));

    /* An ordinary restart after that is not counted as a reset. */
    nv_encode(&r, img);
    TEST_ASSERT_EQUAL_INT64((int64_t)(NV_RESET_NONE), (int64_t)(nv_boot(&r, img, &valid)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(3), (int64_t)(r.boot_count));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(r.reset_count));
    TEST_ASSERT_EQUAL_INT64((int64_t)(NV_RESET_NONE), (int64_t)(r.last_reset_cause));
}

/* ---- MC/DC (labs/session-07/coverage/mcdc.md) ---------------------------
 * MC-3  supervisor.c, a task missed its heartbeat:
 *       inhibited = (ops->actions_inhibited != NULL && ops->actions_inhibited(arg))
 *       A = an inhibit query is configured, B = the query answers "inhibited".
 *       Outcome observed: ACTION_INHIBITED reported and no restart (true),
 *       or the task restarted (false).                                    */

static const sup_ops_t ops_no_query = { fake_restart, fake_escalate, fake_event, NULL };

static void miss_once(const sup_ops_t *o, int inhibit)
{
    memset(restarts, 0, sizeof(restarts));
    ev_n = 0; escalated_task = -1; now = 0;
    inhibited = inhibit;
    TEST_ASSERT_EQUAL_INT(0, sup_init(&sup, 2000, 300000, 3, o, NULL));
    TEST_ASSERT_EQUAL_INT(0, sup_register(&sup, "ACS", 100, 11, 2));
    cycle(1, 0, 0);       /* first interval: establish the baseline */
    cycle(0, 0, 0);       /* ACS silent for a whole interval */
}

static void test_mcdc_MC3_case1_A1_B1_inhibited_no_restart(void)
{
    miss_once(&ops, 1);
    TEST_ASSERT_EQUAL_INT(1, count_events(SUP_EVT_ACTION_INHIBITED));
    TEST_ASSERT_EQUAL_INT(0, restarts[0]);
}

static void test_mcdc_MC3_case2_A0_B1_no_query_restarts(void)
{
    miss_once(&ops_no_query, 1);  /* "inhibited" would answer 1, but no query */
    TEST_ASSERT_EQUAL_INT(0, count_events(SUP_EVT_ACTION_INHIBITED));
    TEST_ASSERT_EQUAL_INT(1, restarts[0]);
}

static void test_mcdc_MC3_case3_A1_B0_not_inhibited_restarts(void)
{
    miss_once(&ops, 0);
    TEST_ASSERT_EQUAL_INT(0, count_events(SUP_EVT_ACTION_INHIBITED));
    TEST_ASSERT_EQUAL_INT(1, restarts[0]);
}

/* ---- Added in Session 7 after the first MC/DC measurement (gap G-06) ---- */

static void test_s7_register_rejects_a_ninth_task(void)
{
    supervisor_t s;
    size_t i;
    TEST_ASSERT_EQUAL_INT(0, sup_init(&s, 2000, 300000, 3, &ops, NULL));
    for (i = 0; i < SUP_MAX_TASKS; i++) {
        TEST_ASSERT_EQUAL_INT((int)i, sup_register(&s, "T", 100, (uint32_t)i, 1));
    }
    TEST_ASSERT_EQUAL_INT(-1, sup_register(&s, "T", 100, 99, 1));   /* full */
}

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_registration_rules);
    RUN_TEST(test_healthy_tasks_raise_nothing);
    RUN_TEST(test_single_missed_heartbeat_restarts_only_that_task);
    RUN_TEST(test_repeated_failure_escalates_after_three_restarts);
    RUN_TEST(test_restarts_outside_the_window_do_not_count);
    RUN_TEST(test_window_is_wrap_safe);
    RUN_TEST(test_inhibit_detects_but_does_not_act);
    RUN_TEST(test_lifting_inhibit_acts_on_a_task_still_silent);
    RUN_TEST(test_watchdog_kicked_never_fires);
    RUN_TEST(test_watchdog_warns_then_resets);
    RUN_TEST(test_watchdog_late_kick_cannot_cancel_reset);
    RUN_TEST(test_nv_round_trip);
    RUN_TEST(test_nv_corruption_falls_back_to_cold_start);
    RUN_TEST(test_nv_boot_sequence);
    RUN_TEST(test_mcdc_MC3_case1_A1_B1_inhibited_no_restart);
    RUN_TEST(test_mcdc_MC3_case2_A0_B1_no_query_restarts);
    RUN_TEST(test_mcdc_MC3_case3_A1_B0_not_inhibited_restarts);
    RUN_TEST(test_s7_register_rejects_a_ninth_task);
    return UNITY_END();
}
