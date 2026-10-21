/*
 * Lab 5.3 - Mode management, FDIR inhibit and schedule tests.
 * Build and run: make test
 */
#include "../modes.h"
#include "../schedule.h"
#include "test_util.h"

/* ---- Recording callbacks ------------------------------------------------- */

static int applied[3];
static vmode_t last_applied;
static int transitions;
static vmode_t tr_from, tr_to;
static cause_src_t tr_src;
static uint16_t tr_detail;
static int inhibit_events;
static int inh_state;
static inhibit_reason_t inh_reason;

static void apply(void *arg, vmode_t mode)
{
    (void)arg;
    applied[mode]++;
    last_applied = mode;
}
static void transition(void *arg, vmode_t from, vmode_t to, cause_src_t src,
                       uint16_t detail)
{
    (void)arg;
    transitions++;
    tr_from = from; tr_to = to; tr_src = src; tr_detail = detail;
    /* Entry actions must already be in place when this is reported. */
    CHECK_EQ(last_applied, to);
}
static void inhibit(void *arg, int on, uint16_t dur, inhibit_reason_t why)
{
    (void)arg; (void)dur;
    inhibit_events++;
    inh_state = on;
    inh_reason = why;
}

static const mode_ops_t ops = { apply, transition, inhibit };
static mode_mgr_t m;

static void setup(vmode_t start)
{
    memset(applied, 0, sizeof(applied));
    transitions = 0; inhibit_events = 0;
    mode_init(&m, start, CAUSE_BOOT, 0, &ops, NULL);
}

/* ---- Transitions -------------------------------------------------------- */

static void test_init_applies_mode_without_event(void)
{
    setup(MODE_NOMINAL);
    CHECK_EQ(applied[MODE_NOMINAL], 1);
    CHECK_EQ(transitions, 0);
    setup(MODE_SAFE);                     /* boot after a reset */
    CHECK_EQ(m.mode, MODE_SAFE);
    CHECK_EQ(applied[MODE_SAFE], 1);
}

static void test_autonomous_escalation_path_with_causes(void)
{
    setup(MODE_NOMINAL);
    CHECK_EQ(mode_request(&m, MODE_DEGRADED, CAUSE_MONITOR, 2, 1000),
             MODE_CHANGED);
    CHECK_EQ(tr_from, MODE_NOMINAL); CHECK_EQ(tr_to, MODE_DEGRADED);
    CHECK_EQ(tr_src, CAUSE_MONITOR); CHECK_EQ(tr_detail, 2);
    CHECK_EQ(m.last_src, CAUSE_MONITOR); CHECK_EQ(m.last_detail, 2);

    CHECK_EQ(mode_request(&m, MODE_SAFE, CAUSE_LIVENESS, 0, 2000),
             MODE_CHANGED);
    CHECK_EQ(tr_to, MODE_SAFE); CHECK_EQ(tr_src, CAUSE_LIVENESS);
    CHECK_EQ(transitions, 2);
    CHECK_EQ(m.last_change_ms, 2000);
}

static void test_autonomous_requests_never_go_back(void)
{
    setup(MODE_SAFE);
    CHECK_EQ(mode_request(&m, MODE_NOMINAL, CAUSE_MONITOR, 1, 0), MODE_NO_CHANGE);
    CHECK_EQ(mode_request(&m, MODE_DEGRADED, CAUSE_LIVENESS, 1, 0), MODE_NO_CHANGE);
    CHECK_EQ(mode_request(&m, MODE_SAFE, CAUSE_LIVENESS, 1, 0), MODE_NO_CHANGE);
    CHECK_EQ(m.mode, MODE_SAFE);
    CHECK_EQ(transitions, 0);

    setup(MODE_DEGRADED);
    CHECK_EQ(mode_request(&m, MODE_DEGRADED, CAUSE_MONITOR, 3, 0), MODE_NO_CHANGE);
    CHECK_EQ(mode_request(&m, MODE_NOMINAL, CAUSE_MONITOR, 3, 0), MODE_NO_CHANGE);
    CHECK_EQ(transitions, 0);
}

static void test_safe_is_left_only_by_ground(void)
{
    uint32_t t;
    setup(MODE_NOMINAL);
    CHECK_EQ(mode_request(&m, MODE_SAFE, CAUSE_LIVENESS, 0, 0), MODE_CHANGED);
    /* A long time passes with every kind of autonomous input: nothing. */
    for (t = 0; t < 100000000u; t += 1000000u) {
        mode_tick(&m, t);
        (void)mode_request(&m, MODE_NOMINAL, CAUSE_MONITOR, 1, t);
    }
    CHECK_EQ(m.mode, MODE_SAFE);
    CHECK_EQ(mode_request(&m, MODE_NOMINAL, CAUSE_GROUND, 0, t), MODE_CHANGED);
    CHECK_EQ(tr_from, MODE_SAFE); CHECK_EQ(tr_src, CAUSE_GROUND);
    CHECK_EQ(m.mode, MODE_NOMINAL);
}

static void test_ground_can_command_any_mode(void)
{
    setup(MODE_NOMINAL);
    CHECK_EQ(mode_request(&m, MODE_SAFE, CAUSE_GROUND, 0, 0), MODE_CHANGED);
    CHECK_EQ(mode_request(&m, MODE_DEGRADED, CAUSE_GROUND, 0, 0), MODE_CHANGED);
    CHECK_EQ(mode_request(&m, MODE_NOMINAL, CAUSE_GROUND, 0, 0), MODE_CHANGED);
    CHECK_EQ(mode_request(&m, MODE_NOMINAL, CAUSE_GROUND, 0, 0), MODE_NO_CHANGE);
    CHECK_EQ(transitions, 3);
}

/* ---- Ground privilege is fail closed (review correction) --------------- */

static void test_only_ground_cause_is_privileged(void)
{
    static const cause_src_t non_ground[] = {
        CAUSE_BOOT, CAUSE_MONITOR, CAUSE_LIVENESS, CAUSE_WATCHDOG_RESET,
        (cause_src_t)5, (cause_src_t)99, (cause_src_t)0xFF
    };
    size_t i;

    CHECK_EQ(mode_is_ground(CAUSE_GROUND), 1);
    CHECK_EQ(mode_is_autonomous(CAUSE_GROUND), 0);
    for (i = 0; i < sizeof(non_ground) / sizeof(non_ground[0]); i++) {
        CHECK_EQ(mode_is_ground(non_ground[i]), 0);
        CHECK_EQ(mode_is_autonomous(non_ground[i]), 1);
    }
}

static void test_ground_can_leave_degraded_and_safe(void)
{
    setup(MODE_DEGRADED);
    CHECK_EQ(mode_request(&m, MODE_NOMINAL, CAUSE_GROUND, 0, 0), MODE_CHANGED);
    CHECK_EQ(m.mode, MODE_NOMINAL);

    setup(MODE_SAFE);
    CHECK_EQ(mode_request(&m, MODE_DEGRADED, CAUSE_GROUND, 0, 0), MODE_CHANGED);
    CHECK_EQ(m.mode, MODE_DEGRADED);

    setup(MODE_SAFE);
    CHECK_EQ(mode_request(&m, MODE_NOMINAL, CAUSE_GROUND, 0, 0), MODE_CHANGED);
    CHECK_EQ(m.mode, MODE_NOMINAL);

    /* Ground is never inhibited. */
    setup(MODE_SAFE);
    CHECK_EQ(mode_set_inhibit(&m, 1, 600, 0), 0);
    CHECK_EQ(mode_request(&m, MODE_NOMINAL, CAUSE_GROUND, 0, 1), MODE_CHANGED);
}

static void test_monitor_and_liveness_cannot_de_escalate(void)
{
    static const cause_src_t fdir[] = { CAUSE_MONITOR, CAUSE_LIVENESS };
    size_t i;

    for (i = 0; i < 2; i++) {
        setup(MODE_SAFE);
        CHECK_EQ(mode_request(&m, MODE_DEGRADED, fdir[i], 1, 0), MODE_NO_CHANGE);
        CHECK_EQ(mode_request(&m, MODE_NOMINAL, fdir[i], 1, 0), MODE_NO_CHANGE);
        CHECK_EQ(m.mode, MODE_SAFE);
        setup(MODE_DEGRADED);
        CHECK_EQ(mode_request(&m, MODE_NOMINAL, fdir[i], 1, 0), MODE_NO_CHANGE);
        CHECK_EQ(m.mode, MODE_DEGRADED);
        CHECK_EQ(transitions, 0);
    }
}

static void test_boot_watchdog_and_unknown_causes_get_no_ground_privilege(void)
{
    static const cause_src_t other[] = {
        CAUSE_BOOT, CAUSE_WATCHDOG_RESET, (cause_src_t)5, (cause_src_t)99,
        (cause_src_t)0xFF
    };
    size_t i;

    for (i = 0; i < sizeof(other) / sizeof(other[0]); i++) {
        /* Cannot de-escalate. */
        setup(MODE_SAFE);
        CHECK_EQ(mode_request(&m, MODE_NOMINAL, other[i], 0, 0), MODE_NO_CHANGE);
        CHECK_EQ(mode_request(&m, MODE_DEGRADED, other[i], 0, 0), MODE_NO_CHANGE);
        CHECK_EQ(m.mode, MODE_SAFE);
        setup(MODE_DEGRADED);
        CHECK_EQ(mode_request(&m, MODE_NOMINAL, other[i], 0, 0), MODE_NO_CHANGE);
        CHECK_EQ(m.mode, MODE_DEGRADED);

        /* Cannot bypass the inhibit either. */
        setup(MODE_NOMINAL);
        CHECK_EQ(mode_set_inhibit(&m, 1, 600, 0), 0);
        CHECK_EQ(mode_request(&m, MODE_SAFE, other[i], 0, 1), MODE_REFUSED_INHIBIT);
        CHECK_EQ(m.mode, MODE_NOMINAL);
        CHECK_EQ(transitions, 0);
    }
}

static void test_target_outside_the_mode_range_is_ignored(void)
{
    setup(MODE_NOMINAL);
    CHECK_EQ(mode_request(&m, (vmode_t)3, CAUSE_GROUND, 0, 0), MODE_NO_CHANGE);
    CHECK_EQ(mode_request(&m, (vmode_t)200, CAUSE_MONITOR, 0, 0), MODE_NO_CHANGE);
    CHECK_EQ(m.mode, MODE_NOMINAL);
    CHECK_EQ(transitions, 0);
}

/* ---- FDIR inhibit -------------------------------------------------------- */

static void test_inhibit_blocks_autonomous_only(void)
{
    setup(MODE_NOMINAL);
    CHECK_EQ(mode_set_inhibit(&m, 1, 600, 0), 0);
    CHECK_EQ(inh_state, 1); CHECK_EQ(inh_reason, INHIBIT_BY_GROUND);
    CHECK_EQ(mode_request(&m, MODE_SAFE, CAUSE_LIVENESS, 0, 10),
             MODE_REFUSED_INHIBIT);
    CHECK_EQ(mode_request(&m, MODE_DEGRADED, CAUSE_MONITOR, 2, 10),
             MODE_REFUSED_INHIBIT);
    CHECK_EQ(m.mode, MODE_NOMINAL);
    CHECK_EQ(mode_request(&m, MODE_SAFE, CAUSE_GROUND, 0, 10), MODE_CHANGED);

    CHECK_EQ(mode_set_inhibit(&m, 0, 0, 20), 0);
    CHECK_EQ(inh_state, 0);
    CHECK_EQ(inhibit_events, 2);
}

static void test_inhibit_expires_by_itself(void)
{
    setup(MODE_NOMINAL);
    CHECK_EQ(mode_set_inhibit(&m, 1, 60, 1000), 0);
    CHECK_EQ(mode_inhibit_remaining_s(&m, 1000), 60);
    CHECK_EQ(mode_inhibit_remaining_s(&m, 30500), 31);
    mode_tick(&m, 60999);
    CHECK_EQ(m.inhibited, 1);
    mode_tick(&m, 61000);
    CHECK_EQ(m.inhibited, 0);
    CHECK_EQ(inh_reason, INHIBIT_BY_TIMEOUT);
    CHECK_EQ(mode_inhibit_remaining_s(&m, 61000), 0);
    mode_tick(&m, 99999);
    CHECK_EQ(inhibit_events, 2);          /* reported once */
    CHECK_EQ(mode_request(&m, MODE_DEGRADED, CAUSE_MONITOR, 2, 100000),
             MODE_CHANGED);
}

static void test_inhibit_duration_is_bounded(void)
{
    setup(MODE_NOMINAL);
    CHECK_EQ(mode_set_inhibit(&m, 1, 0, 0), -1);
    CHECK_EQ(mode_set_inhibit(&m, 1, MODE_INHIBIT_MAX_S + 1u, 0), -1);
    CHECK_EQ(m.inhibited, 0);
    CHECK_EQ(inhibit_events, 0);
    CHECK_EQ(mode_set_inhibit(&m, 1, MODE_INHIBIT_MAX_S, 0), 0);
}

static void test_inhibit_expiry_is_wrap_safe(void)
{
    setup(MODE_NOMINAL);
    CHECK_EQ(mode_set_inhibit(&m, 1, 10, 0xFFFFF000u), 0);
    mode_tick(&m, 0xFFFFFFFFu);
    CHECK_EQ(m.inhibited, 1);
    mode_tick(&m, 0x00002000u);           /* wrapped, 10 s later */
    CHECK_EQ(m.inhibited, 0);
}

/* ---- Schedule ------------------------------------------------------------- */

static int rel_n;
static uint8_t rel_first[8];
static void on_release(const uint8_t *tc, size_t len, void *arg)
{
    (void)len; (void)arg;
    if (rel_n < 8) { rel_first[rel_n] = tc[0]; }
    rel_n++;
}

static void test_schedule_releases_due_activities_in_time_order(void)
{
    sched_t s;
    uint8_t a = 'A', b = 'B', c = 'C';
    sched_init(&s);
    CHECK_EQ(sched_insert(&s, 30, &c, 1), 0);
    CHECK_EQ(sched_insert(&s, 10, &a, 1), 0);
    CHECK_EQ(sched_insert(&s, 20, &b, 1), 0);
    rel_n = 0;
    CHECK_EQ(sched_release_due(&s, 5, on_release, NULL), 0);
    CHECK_EQ(sched_release_due(&s, 25, on_release, NULL), 2);
    CHECK_EQ(rel_first[0], 'A'); CHECK_EQ(rel_first[1], 'B');
    CHECK_EQ(sched_count(&s), 1);
}

static void test_suspended_schedule_releases_nothing(void)
{
    sched_t s;
    uint8_t a = 'A';
    sched_init(&s);
    CHECK_EQ(sched_insert(&s, 10, &a, 1), 0);
    sched_set_enabled(&s, 0);                     /* entering SAFE */
    rel_n = 0;
    CHECK_EQ(sched_release_due(&s, 1000, on_release, NULL), 0);
    CHECK_EQ(rel_n, 0);
    CHECK_EQ(sched_count(&s), 1);                 /* kept, not lost */
    sched_set_enabled(&s, 1);                     /* ground decision */
    CHECK_EQ(sched_release_due(&s, 1000, on_release, NULL), 1);
}

static void test_schedule_limits_and_reset(void)
{
    sched_t s;
    uint8_t big[SCHED_MAX_TC + 1u] = { 0 };
    size_t i;
    sched_init(&s);
    CHECK_EQ(sched_insert(&s, 1, big, 0), -2);
    CHECK_EQ(sched_insert(&s, 1, big, SCHED_MAX_TC + 1u), -2);
    for (i = 0; i < SCHED_MAX_ENTRIES; i++) {
        CHECK_EQ(sched_insert(&s, 1, big, SCHED_MAX_TC), 0);
    }
    CHECK_EQ(sched_insert(&s, 1, big, 1), -1);
    sched_reset(&s);
    CHECK_EQ(sched_count(&s), 0);
}

/* A released command that disables the schedule stops further release
 * in the same pass (the SAFE entry case). */
static sched_t *reentrant;
static void disable_on_release(const uint8_t *tc, size_t len, void *arg)
{
    (void)tc; (void)len; (void)arg;
    rel_n++;
    sched_set_enabled(reentrant, 0);
}

static void test_release_that_suspends_stops_the_pass(void)
{
    sched_t s;
    uint8_t a = 'A';
    sched_init(&s);
    reentrant = &s;
    CHECK_EQ(sched_insert(&s, 1, &a, 1), 0);
    CHECK_EQ(sched_insert(&s, 2, &a, 1), 0);
    rel_n = 0;
    CHECK_EQ(sched_release_due(&s, 10, disable_on_release, NULL), 1);
    CHECK_EQ(sched_count(&s), 1);
}

int main(void)
{
    printf("Lab 5.3 - mode management, FDIR inhibit and schedule tests\n\n");
    RUN(test_init_applies_mode_without_event);
    RUN(test_autonomous_escalation_path_with_causes);
    RUN(test_autonomous_requests_never_go_back);
    RUN(test_safe_is_left_only_by_ground);
    RUN(test_ground_can_command_any_mode);
    RUN(test_only_ground_cause_is_privileged);
    RUN(test_ground_can_leave_degraded_and_safe);
    RUN(test_monitor_and_liveness_cannot_de_escalate);
    RUN(test_boot_watchdog_and_unknown_causes_get_no_ground_privilege);
    RUN(test_target_outside_the_mode_range_is_ignored);
    RUN(test_inhibit_blocks_autonomous_only);
    RUN(test_inhibit_expires_by_itself);
    RUN(test_inhibit_duration_is_bounded);
    RUN(test_inhibit_expiry_is_wrap_safe);
    RUN(test_schedule_releases_due_activities_in_time_order);
    RUN(test_suspended_schedule_releases_nothing);
    RUN(test_schedule_limits_and_reset);
    RUN(test_release_that_suspends_stops_the_pass);
    return TEST_SUMMARY();
}
