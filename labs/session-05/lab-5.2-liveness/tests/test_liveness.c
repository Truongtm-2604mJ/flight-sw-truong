/*
 * Lab 5.2 - Supervisor, watchdog and NV record tests.
 * Build and run: make test
 */
#include "../supervisor.h"
#include "../watchdog.h"
#include "../nvstate.h"
#include "test_util.h"

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
    CHECK_EQ(sup_init(&sup, 2000, 300000, 3, &ops, NULL), 0);
    CHECK_EQ(sup_register(&sup, "ACS", 100, 11, 2), 0);
    CHECK_EQ(sup_register(&sup, "HK", 500, 12, 2), 1);
    CHECK_EQ(sup_register(&sup, "LOG", 1000, 13, 1), 2);
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
    CHECK_EQ(sup_init(&s, 2000, 300000, 0, &ops, NULL), -1);
    CHECK_EQ(sup_init(&s, 2000, 300000, SUP_MAX_RESTARTS + 1, &ops, NULL), -1);
    CHECK_EQ(sup_init(&s, 2000, 300000, 3, &ops, NULL), 0);
    /* A task as slow as the supervisor could miss a whole interval
     * while healthy: refused. */
    CHECK_EQ(sup_register(&s, "SLOW", 2000, 1, 0), -1);
    CHECK_EQ(sup_register(&s, "OK", 1999, 1, 0), 0);
}

static void test_healthy_tasks_raise_nothing(void)
{
    int i;
    setup();
    for (i = 0; i < 1000; i++) { cycle(1, 1, 1); }  /* 2000 s */
    CHECK_EQ(ev_n, 0);
    CHECK_EQ(restarts[0] + restarts[1] + restarts[2], 0);
}

static void test_single_missed_heartbeat_restarts_only_that_task(void)
{
    setup();
    cycle(1, 1, 1);
    cycle(1, 1, 0);                      /* LOG silent for one interval */
    CHECK_EQ(count_events(SUP_EVT_HEARTBEAT_MISSED), 1);
    CHECK_EQ(count_events(SUP_EVT_TASK_RESTARTED), 1);
    CHECK_EQ(ev_task[0], 2);             /* isolated to LOG */
    CHECK_EQ(restarts[2], 1);
    CHECK_EQ(restarts[0] + restarts[1], 0);
    cycle(1, 1, 1);                      /* recovered */
    cycle(1, 1, 1);
    CHECK_EQ(ev_n, 2);
    CHECK_EQ(sup.tasks[2].status, SUP_TASK_OK);
}

static void test_repeated_failure_escalates_after_three_restarts(void)
{
    int i;
    setup();
    cycle(1, 1, 1);
    for (i = 0; i < 5; i++) { cycle(0, 1, 1); }   /* ACS keeps failing */

    CHECK_EQ(restarts[0], 3);                     /* bounded           */
    CHECK_EQ(count_events(SUP_EVT_TASK_RESTARTED), 3);
    CHECK_EQ(count_events(SUP_EVT_TASK_ESCALATED), 1);
    CHECK_EQ(escalated_task, 0);
    CHECK_EQ(escalated_to, 2);
    CHECK_EQ(sup.tasks[0].status, SUP_TASK_FAILED);
    /* 4 misses reported (3 restarted + 1 escalated); after FAILED the
     * task is no longer checked, so the 5th interval is silent. */
    CHECK_EQ(count_events(SUP_EVT_HEARTBEAT_MISSED), 4);
    CHECK_EQ(restarts[1] + restarts[2], 0);
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
    CHECK_EQ(restarts[0], 10);
    CHECK_EQ(count_events(SUP_EVT_TASK_ESCALATED), 0);
    CHECK(sup_restarts_in_window(&sup, 0, now) <= 2);

    /* Failures 90 s apart: three restarts at 0, 90 and 180 s; the
     * fourth failure (270 s) finds all three inside 300 s and escalates. */
    setup();
    for (i = 0; i < 4; i++) {
        int k;
        cycle(0, 1, 1);
        for (k = 0; k < 44; k++) { cycle(1, 1, 1); }
    }
    CHECK_EQ(restarts[0], 3);
    CHECK_EQ(escalated_task, 0);

    /* Boundary: 100 s apart puts the first restart exactly 300 s before
     * the fourth failure, outside the window, so it restarts again. */
    setup();
    for (i = 0; i < 4; i++) {
        int k;
        cycle(0, 1, 1);
        for (k = 0; k < 49; k++) { cycle(1, 1, 1); }
    }
    CHECK_EQ(restarts[0], 4);
    CHECK_EQ(escalated_task, -1);
}

static void test_window_is_wrap_safe(void)
{
    int i;
    setup();
    now = 0xFFFFFFFFu - 3000u;           /* 32 bit ms counter about to wrap */
    for (i = 0; i < 4; i++) { cycle(1, 0, 1); }
    CHECK_EQ(restarts[1], 3);
    CHECK_EQ(escalated_task, 1);
}

static void test_inhibit_detects_but_does_not_act(void)
{
    int i;
    setup();
    inhibited = 1;
    for (i = 0; i < 10; i++) { cycle(1, 1, 0); }
    CHECK_EQ(restarts[2], 0);
    CHECK_EQ(escalated_task, -1);
    /* Reported once per episode, not every interval. */
    CHECK_EQ(count_events(SUP_EVT_HEARTBEAT_MISSED), 1);
    CHECK_EQ(count_events(SUP_EVT_ACTION_INHIBITED), 1);
    CHECK_EQ(sup.tasks[2].status, SUP_TASK_MISSING);

    cycle(1, 1, 1);                      /* comes back by itself */
    CHECK_EQ(count_events(SUP_EVT_TASK_RECOVERED), 1);
    CHECK_EQ(sup.tasks[2].status, SUP_TASK_OK);

    inhibited = 0;                       /* actions allowed again */
    cycle(1, 1, 0);
    CHECK_EQ(restarts[2], 1);
}

static void test_lifting_inhibit_acts_on_a_task_still_silent(void)
{
    setup();
    inhibited = 1;
    cycle(1, 1, 0);
    cycle(1, 1, 0);
    CHECK_EQ(restarts[2], 0);
    inhibited = 0;                       /* ground lifts the inhibit */
    cycle(1, 1, 0);                      /* LOG is still silent      */
    CHECK_EQ(restarts[2], 1);
    /* The miss was already reported: no duplicate HEARTBEAT_MISSED. */
    CHECK_EQ(count_events(SUP_EVT_HEARTBEAT_MISSED), 1);
    CHECK_EQ(count_events(SUP_EVT_TASK_RESTARTED), 1);
    CHECK_EQ(sup.tasks[2].status, SUP_TASK_OK);
}

/* ---- Watchdog ----------------------------------------------------------- */

static void test_watchdog_kicked_never_fires(void)
{
    wdg_t w;
    int i;
    wdg_init(&w, 5000, 1000);
    for (i = 0; i < 10000; i++) {
        CHECK_EQ(wdg_tick(&w, 100), WDG_NONE);
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
    CHECK_EQ(warn_at, 5000);
    CHECK_EQ(reset_at, 6000);
    CHECK_EQ(w.state, WDG_EXPIRED);
}

static void test_watchdog_late_kick_cannot_cancel_reset(void)
{
    wdg_t w;
    int i, resets = 0;
    wdg_init(&w, 5000, 1000);
    for (i = 0; i < 50; i++) { (void)wdg_tick(&w, 100); }
    CHECK_EQ(w.state, WDG_WARNED);
    wdg_kick(&w);                         /* too late */
    for (i = 0; i < 20; i++) { resets += (wdg_tick(&w, 100) == WDG_RESET); }
    CHECK_EQ(resets, 1);
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
    CHECK_EQ(img[0], 'F'); CHECK_EQ(img[3], 'R');
    CHECK_EQ(nv_decode(img, &b), 0);
    CHECK_MEM(&a, &b, sizeof(a));
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
        CHECK_EQ(nv_decode(bad, &r), -1);
        CHECK_EQ(r.boot_count, 0);
        CHECK_EQ(r.last_esc_task, 0xFF);
    }
    memset(img, 0, sizeof(img));           /* never written */
    CHECK_EQ(nv_decode(img, &r), -1);
}

static void test_nv_boot_sequence(void)
{
    nv_record_t r;
    uint8_t img[NV_IMAGE_LEN];
    int valid;

    /* Blank memory: cold start, boot 1. */
    memset(img, 0xFF, sizeof(img));
    CHECK_EQ(nv_boot(&r, img, &valid), NV_RESET_NONE);
    CHECK_EQ(valid, 0);
    CHECK_EQ(r.boot_count, 1);

    /* Watchdog records the cause, then the processor resets. */
    r.pending_reset = NV_RESET_WATCHDOG;
    nv_encode(&r, img);
    CHECK_EQ(nv_boot(&r, img, &valid), NV_RESET_WATCHDOG);
    CHECK_EQ(valid, 1);
    CHECK_EQ(r.boot_count, 2);
    CHECK_EQ(r.reset_count, 1);
    CHECK_EQ(r.last_reset_cause, NV_RESET_WATCHDOG);
    CHECK_EQ(r.pending_reset, NV_RESET_NONE);

    /* An ordinary restart after that is not counted as a reset. */
    nv_encode(&r, img);
    CHECK_EQ(nv_boot(&r, img, &valid), NV_RESET_NONE);
    CHECK_EQ(r.boot_count, 3);
    CHECK_EQ(r.reset_count, 1);
    CHECK_EQ(r.last_reset_cause, NV_RESET_NONE);
}

int main(void)
{
    printf("Lab 5.2 - liveness supervision, watchdog and NV record tests\n\n");
    RUN(test_registration_rules);
    RUN(test_healthy_tasks_raise_nothing);
    RUN(test_single_missed_heartbeat_restarts_only_that_task);
    RUN(test_repeated_failure_escalates_after_three_restarts);
    RUN(test_restarts_outside_the_window_do_not_count);
    RUN(test_window_is_wrap_safe);
    RUN(test_inhibit_detects_but_does_not_act);
    RUN(test_lifting_inhibit_acts_on_a_task_still_silent);
    RUN(test_watchdog_kicked_never_fires);
    RUN(test_watchdog_warns_then_resets);
    RUN(test_watchdog_late_kick_cannot_cancel_reset);
    RUN(test_nv_round_trip);
    RUN(test_nv_corruption_falls_back_to_cold_start);
    RUN(test_nv_boot_sequence);
    return TEST_SUMMARY();
}
