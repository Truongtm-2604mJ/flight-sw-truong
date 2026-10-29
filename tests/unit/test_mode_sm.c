/* Unit suite: generated vehicle mode state machine (mode_sm.c, model
 * vehicle_mode v3), tested through its step function.
 *
 * The module is generated (labs/session-06/lab-6.2-generation); these tests
 * check it against the model's stated behaviour, transition by transition,
 * plus the consumption rules and the MC/DC cases of the guarded
 * transitions (labs/session-07/coverage/mcdc.md, decisions MC-4 and MC-5).
 */
#include <stdint.h>
#include <string.h>

#include "mode_sm.h"
#include "unity.h"

/* ---- Recording hooks ---------------------------------------------------- */

typedef struct {
    int payload_on, sun, sched, hk_rate, mon_fast, rx;
    int entries;             /* entry actions run, all states */
    int exits;
    int transitions, refusals, discards, invalid;
    unsigned discard_mask;
    mode_sm_state_t from, to;
    mode_sm_event_t event;
    uint8_t src;
    uint16_t detail;
} obs_t;

static obs_t o;
static mode_sm_t sm;

#define HOOK(name, body) \
    void mode_sm_action_##name(void *user, const mode_sm_payload_t *cause) \
    { (void)user; (void)cause; body; }

HOOK(payload_enable,       o.payload_on = 1; o.entries++)
HOOK(payload_suspend,      o.payload_on = 0; o.entries++)
HOOK(payload_off,          o.payload_on = 0; o.entries++)
HOOK(hk_rate_normal,       o.hk_rate = 0; o.mon_fast = 0; o.entries++)
HOOK(hk_rate_high,         o.hk_rate = 1; o.entries++)
HOOK(hk_minimal,           o.hk_rate = 2; o.entries++)
HOOK(monitors_tighten,     o.mon_fast = 1; o.entries++)
HOOK(schedule_suspend,     o.sched = 0; o.entries++)
HOOK(sun_pointing,         o.sun = 1; o.entries++)
HOOK(sun_pointing_release, o.sun = 0; o.exits++)
HOOK(rx_guarantee,         o.rx = 1; o.entries++)
HOOK(report_action_inhibited, o.refusals++; o.src = cause->src)

void mode_sm_on_transition(void *user, mode_sm_state_t from, mode_sm_state_t to,
                           mode_sm_event_t event, const mode_sm_payload_t *cause)
{
    (void)user;
    o.transitions++;
    o.from = from;
    o.to = to;
    o.event = event;
    o.src = cause->src;
    o.detail = cause->detail;
}

void mode_sm_on_discard(void *user, mode_sm_event_t event, const mode_sm_payload_t *c)
{
    (void)user; (void)c;
    o.discards++;
    o.discard_mask |= 1u << (unsigned)event;
}

void mode_sm_on_invalid_state(void *user, uint32_t raw)
{
    (void)user; (void)raw;
    o.invalid++;
}

/* ---- Helpers ------------------------------------------------------------- */

enum { GROUND = 1, MONITOR = 2 };

static void boot(mode_sm_state_t s)
{
    memset(&o, 0, sizeof(o));
    o.sched = 1;
    TEST_ASSERT_EQUAL_INT(0, mode_sm_init(&sm, s, NULL));
}

static void post(mode_sm_event_t e, uint8_t src)
{
    mode_sm_payload_t p;
    p.src = src;
    p.detail = 7u;
    TEST_ASSERT_EQUAL_INT(0, mode_sm_post(&sm, e, p));
}

static mode_sm_result_t step(int inhibited)
{
    mode_sm_inputs_t in;
    in.fdir_inhibited = inhibited != 0;
    return mode_sm_step(&sm, &in);
}

/* Reach a state through the ground path, then clear the observations. */
static void reach(mode_sm_state_t s)
{
    boot(MODE_SM_NOMINAL);
    if (s == MODE_SM_DEGRADED) {
        post(MODE_SM_EV_TC_MODE_DEGRADED, GROUND);
        (void)step(0);
    } else if (s == MODE_SM_SAFE) {
        post(MODE_SM_EV_TC_MODE_SAFE, GROUND);
        (void)step(0);
    }
    TEST_ASSERT_EQUAL_INT(s, mode_sm_state(&sm));
    o.transitions = o.refusals = o.discards = o.entries = o.exits = 0;
    o.discard_mask = 0u;
}

static void expect_transition(mode_sm_result_t r, unsigned tid,
                              mode_sm_state_t to, mode_sm_event_t ev)
{
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_TRANSITION, r.kind);
    TEST_ASSERT_EQUAL_UINT(tid, r.transition);
    TEST_ASSERT_EQUAL_INT(to, r.to);
    TEST_ASSERT_EQUAL_INT(to, mode_sm_state(&sm));
    TEST_ASSERT_EQUAL_INT(1, o.transitions);
    TEST_ASSERT_EQUAL_INT(ev, o.event);
}

/* ---- Boot and API contract ------------------------------------------------ */

static void test_boot_nominal_runs_entry_actions_without_report(void)
{
    boot(MODE_SM_NOMINAL);
    TEST_ASSERT_EQUAL_INT(MODE_SM_NOMINAL, mode_sm_state(&sm));
    TEST_ASSERT_EQUAL_INT(1, o.payload_on);
    TEST_ASSERT_EQUAL_INT(0, o.transitions);
}

static void test_boot_safe_after_watchdog_reset(void)
{
    boot(MODE_SM_SAFE);
    TEST_ASSERT_EQUAL_INT(MODE_SM_SAFE, mode_sm_state(&sm));
    TEST_ASSERT_EQUAL_INT(0, o.payload_on);
    TEST_ASSERT_EQUAL_INT(1, o.sun);
    TEST_ASSERT_EQUAL_INT(0, o.sched);
    TEST_ASSERT_EQUAL_INT(0, o.transitions);
}

static void test_boot_in_degraded_is_refused_and_enters_safe(void)
{
    memset(&o, 0, sizeof(o));
    TEST_ASSERT_EQUAL_INT(-1, mode_sm_init(&sm, MODE_SM_DEGRADED, NULL));
    TEST_ASSERT_EQUAL_INT(MODE_SM_SAFE, mode_sm_state(&sm));
}

static void test_post_rejects_event_outside_the_model(void)
{
    mode_sm_payload_t p = { 1u, 0u };
    boot(MODE_SM_NOMINAL);
    TEST_ASSERT_EQUAL_INT(-1, mode_sm_post(&sm, MODE_SM_EVENT_COUNT, p));
    TEST_ASSERT_EQUAL_INT(-1, mode_sm_post(&sm, (mode_sm_event_t)200, p));
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_IDLE, step(0).kind);
}

static void test_step_with_nothing_pending_is_idle(void)
{
    boot(MODE_SM_NOMINAL);
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_IDLE, step(0).kind);
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_IDLE, step(1).kind);
    TEST_ASSERT_EQUAL_INT(0, o.transitions);
}

/* ---- Every transition of the model ------------------------------------------- */

static void test_T01_nominal_to_safe_on_ground(void)
{
    reach(MODE_SM_NOMINAL);
    post(MODE_SM_EV_TC_MODE_SAFE, GROUND);
    expect_transition(step(1), 1u, MODE_SM_SAFE, MODE_SM_EV_TC_MODE_SAFE);
    TEST_ASSERT_EQUAL_INT(GROUND, o.src);
    TEST_ASSERT_EQUAL_INT(1, o.sun);
    TEST_ASSERT_EQUAL_INT(1, o.mon_fast);    /* D-01 */
    TEST_ASSERT_EQUAL_INT(1, o.rx);
}

static void test_T02_nominal_to_safe_on_critical_fault(void)
{
    reach(MODE_SM_NOMINAL);
    post(MODE_SM_EV_FAULT_CRITICAL, MONITOR);
    expect_transition(step(0), 2u, MODE_SM_SAFE, MODE_SM_EV_FAULT_CRITICAL);
    TEST_ASSERT_EQUAL_INT(MONITOR, o.src);
    TEST_ASSERT_EQUAL_UINT16(7u, o.detail);
}

static void test_T03_nominal_to_degraded_on_ground(void)
{
    reach(MODE_SM_NOMINAL);
    post(MODE_SM_EV_TC_MODE_DEGRADED, GROUND);
    expect_transition(step(1), 3u, MODE_SM_DEGRADED, MODE_SM_EV_TC_MODE_DEGRADED);
    TEST_ASSERT_EQUAL_INT(0, o.payload_on);
    TEST_ASSERT_EQUAL_INT(1, o.hk_rate);
}

static void test_T04_nominal_to_degraded_on_noncritical_fault(void)
{
    reach(MODE_SM_NOMINAL);
    post(MODE_SM_EV_FAULT_NONCRITICAL, MONITOR);
    expect_transition(step(0), 4u, MODE_SM_DEGRADED, MODE_SM_EV_FAULT_NONCRITICAL);
}

static void test_T05_T06_inhibited_faults_in_nominal_are_refused_and_reported(void)
{
    mode_sm_result_t r;
    reach(MODE_SM_NOMINAL);
    post(MODE_SM_EV_FAULT_CRITICAL, MONITOR);
    r = step(1);
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_INTERNAL, r.kind);
    TEST_ASSERT_EQUAL_UINT(5u, r.transition);
    post(MODE_SM_EV_FAULT_NONCRITICAL, MONITOR);
    r = step(1);
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_INTERNAL, r.kind);
    TEST_ASSERT_EQUAL_UINT(6u, r.transition);
    TEST_ASSERT_EQUAL_INT(MODE_SM_NOMINAL, mode_sm_state(&sm));
    TEST_ASSERT_EQUAL_INT(2, o.refusals);
    TEST_ASSERT_EQUAL_INT(0, o.transitions);
    TEST_ASSERT_EQUAL_INT(0, o.entries);     /* internal: no entry actions */
}

static void test_T07_T08_degraded_to_safe(void)
{
    reach(MODE_SM_DEGRADED);
    post(MODE_SM_EV_TC_MODE_SAFE, GROUND);
    expect_transition(step(0), 7u, MODE_SM_SAFE, MODE_SM_EV_TC_MODE_SAFE);
    reach(MODE_SM_DEGRADED);
    post(MODE_SM_EV_FAULT_CRITICAL, MONITOR);
    expect_transition(step(0), 8u, MODE_SM_SAFE, MODE_SM_EV_FAULT_CRITICAL);
}

static void test_T09_degraded_to_nominal_on_ground_only(void)
{
    reach(MODE_SM_DEGRADED);
    post(MODE_SM_EV_TC_MODE_NOMINAL, GROUND);
    expect_transition(step(0), 9u, MODE_SM_NOMINAL, MODE_SM_EV_TC_MODE_NOMINAL);
    TEST_ASSERT_EQUAL_INT(1, o.payload_on);
}

static void test_T10_inhibited_critical_fault_in_degraded_is_refused(void)
{
    mode_sm_result_t r;
    reach(MODE_SM_DEGRADED);
    post(MODE_SM_EV_FAULT_CRITICAL, MONITOR);
    r = step(1);
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_INTERNAL, r.kind);
    TEST_ASSERT_EQUAL_UINT(10u, r.transition);
    TEST_ASSERT_EQUAL_INT(MODE_SM_DEGRADED, mode_sm_state(&sm));
    TEST_ASSERT_EQUAL_INT(1, o.refusals);
}

static void test_T11_T12_safe_left_by_ground_with_exit_action(void)
{
    reach(MODE_SM_SAFE);
    post(MODE_SM_EV_TC_MODE_DEGRADED, GROUND);
    expect_transition(step(0), 11u, MODE_SM_DEGRADED, MODE_SM_EV_TC_MODE_DEGRADED);
    TEST_ASSERT_EQUAL_INT(1, o.exits);
    TEST_ASSERT_EQUAL_INT(0, o.sun);         /* A5: released on exit */
    reach(MODE_SM_SAFE);
    post(MODE_SM_EV_TC_MODE_NOMINAL, GROUND);
    expect_transition(step(0), 12u, MODE_SM_NOMINAL, MODE_SM_EV_TC_MODE_NOMINAL);
    TEST_ASSERT_EQUAL_INT(0, o.sun);
    TEST_ASSERT_EQUAL_INT(0, o.sched);       /* leaving SAFE does not resume */
}

/* ---- Ignore lists and consumption ------------------------------------------- */

static void test_autonomous_events_never_leave_safe(void)
{
    int k, inh;
    reach(MODE_SM_SAFE);
    for (k = 0; k < 20; k++) {
        for (inh = 0; inh < 2; inh++) {
            post(MODE_SM_EV_FAULT_CRITICAL, MONITOR);
            post(MODE_SM_EV_FAULT_NONCRITICAL, MONITOR);
            post(MODE_SM_EV_FAULT_CLEARED, MONITOR);
            TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_IDLE, step(inh).kind);
        }
    }
    TEST_ASSERT_EQUAL_INT(MODE_SM_SAFE, mode_sm_state(&sm));
    TEST_ASSERT_EQUAL_INT(0, o.transitions + o.refusals + o.discards);
}

static void test_command_for_current_mode_is_ignored_silently(void)
{
    reach(MODE_SM_DEGRADED);
    post(MODE_SM_EV_TC_MODE_DEGRADED, GROUND);
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_IDLE, step(0).kind);
    TEST_ASSERT_EQUAL_INT(0, o.entries);     /* entry actions not re-run */
    TEST_ASSERT_EQUAL_INT(0, o.discards);
}

static void test_ground_command_losing_to_priority_is_reported(void)
{
    reach(MODE_SM_NOMINAL);
    post(MODE_SM_EV_TC_MODE_DEGRADED, GROUND);
    post(MODE_SM_EV_FAULT_CRITICAL, MONITOR);
    expect_transition(step(0), 2u, MODE_SM_SAFE, MODE_SM_EV_FAULT_CRITICAL);
    TEST_ASSERT_EQUAL_UINT(1u << MODE_SM_EV_TC_MODE_DEGRADED, o.discard_mask);
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_IDLE, step(0).kind);   /* not replayed */
}

static void test_D05_ignored_ground_command_is_reported_when_mode_changes(void)
{
    reach(MODE_SM_NOMINAL);
    post(MODE_SM_EV_TC_MODE_DEGRADED, GROUND);
    post(MODE_SM_EV_TC_MODE_NOMINAL, GROUND);
    expect_transition(step(0), 3u, MODE_SM_DEGRADED, MODE_SM_EV_TC_MODE_DEGRADED);
    TEST_ASSERT_EQUAL_UINT(1u << MODE_SM_EV_TC_MODE_NOMINAL, o.discard_mask);
}

static void test_ignored_ground_command_stays_silent_on_internal_transition(void)
{
    /* NOMINAL, inhibited: a critical fault is refused (T05) and the vehicle
     * stays NOMINAL, so a ground NOMINAL in the same cycle is satisfied. */
    reach(MODE_SM_NOMINAL);
    post(MODE_SM_EV_TC_MODE_NOMINAL, GROUND);
    post(MODE_SM_EV_FAULT_CRITICAL, MONITOR);
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_INTERNAL, step(1).kind);
    TEST_ASSERT_EQUAL_INT(0, o.discards);
}

static void test_D04_fault_latched_across_ground_deescalation(void)
{
    reach(MODE_SM_DEGRADED);
    post(MODE_SM_EV_FAULT_NONCRITICAL, MONITOR);
    post(MODE_SM_EV_TC_MODE_NOMINAL, GROUND);
    expect_transition(step(0), 9u, MODE_SM_NOMINAL, MODE_SM_EV_TC_MODE_NOMINAL);
    o.transitions = 0;
    expect_transition(step(0), 4u, MODE_SM_DEGRADED, MODE_SM_EV_FAULT_NONCRITICAL);
}

static void test_latched_fault_ignored_in_start_state_is_dropped_when_idle(void)
{
    reach(MODE_SM_DEGRADED);
    post(MODE_SM_EV_FAULT_NONCRITICAL, MONITOR);
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_IDLE, step(0).kind);
    post(MODE_SM_EV_TC_MODE_NOMINAL, GROUND);
    expect_transition(step(0), 9u, MODE_SM_NOMINAL, MODE_SM_EV_TC_MODE_NOMINAL);
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_IDLE, step(0).kind);    /* not kept */
}

static void test_coalescing_last_payload_wins(void)
{
    mode_sm_payload_t a = { 2u, 11u }, b = { 3u, 22u };
    reach(MODE_SM_NOMINAL);
    TEST_ASSERT_EQUAL_INT(0, mode_sm_post(&sm, MODE_SM_EV_FAULT_CRITICAL, a));
    TEST_ASSERT_EQUAL_INT(0, mode_sm_post(&sm, MODE_SM_EV_FAULT_CRITICAL, b));
    expect_transition(step(0), 2u, MODE_SM_SAFE, MODE_SM_EV_FAULT_CRITICAL);
    TEST_ASSERT_EQUAL_INT(3, o.src);
    TEST_ASSERT_EQUAL_UINT16(22u, o.detail);
}

static void test_invalid_state_recovers_to_safe_keeping_latched_events(void)
{
    mode_sm_result_t r;
    reach(MODE_SM_NOMINAL);
    post(MODE_SM_EV_TC_MODE_DEGRADED, GROUND);
    post(MODE_SM_EV_FAULT_NONCRITICAL, MONITOR);
    sm.state = (mode_sm_state_t)0x5A;        /* simulated memory upset */
    r = step(0);
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_RECOVERED, r.kind);
    TEST_ASSERT_EQUAL_INT(MODE_SM_SAFE, mode_sm_state(&sm));
    TEST_ASSERT_EQUAL_INT(1, o.invalid);
    TEST_ASSERT_EQUAL_UINT32(1u << MODE_SM_EV_FAULT_NONCRITICAL, sm.pending);
}

/* ---- MC/DC: T02 guard, decision MC-4 ----------------------------------------
 * Decision (NOMINAL, T01 not enabled):  A && B
 *   A = fault_critical pending,  B = !fdir_inhibited
 * Independence pairs: A by {1, 2}, B by {1, 3}.                            */

static mode_sm_result_t nominal_with(int fault_critical, int inhibited)
{
    reach(MODE_SM_NOMINAL);
    if (fault_critical) {
        post(MODE_SM_EV_FAULT_CRITICAL, MONITOR);
    }
    return step(inhibited);
}

static void test_mcdc_T02_case1_A1_B1_fires(void)
{
    mode_sm_result_t r = nominal_with(1, 0);
    TEST_ASSERT_EQUAL_UINT(2u, r.transition);
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_TRANSITION, r.kind);
}

static void test_mcdc_T02_case2_A0_B1_does_not_fire(void)
{
    mode_sm_result_t r = nominal_with(0, 0);
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_IDLE, r.kind);
}

static void test_mcdc_T02_case3_A1_B0_does_not_fire(void)
{
    mode_sm_result_t r = nominal_with(1, 1);
    TEST_ASSERT_TRUE(r.transition != 2u);
    TEST_ASSERT_EQUAL_INT(MODE_SM_NOMINAL, mode_sm_state(&sm));
}

/* ---- MC/DC: T05 guard, decision MC-5 ----------------------------------------
 * Decision (NOMINAL, T01..T04 not enabled):  A && C
 *   A = fault_critical pending,  C = fdir_inhibited
 * A by {1, 2} is shown. C cannot be shown: with A true and C false, T02
 * (priority 2) fires and T05 is never evaluated. Justified by construction
 * in labs/session-07/coverage/mcdc.md.                                      */

static void test_mcdc_T05_case1_A1_C1_fires(void)
{
    mode_sm_result_t r = nominal_with(1, 1);
    TEST_ASSERT_EQUAL_UINT(5u, r.transition);
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_INTERNAL, r.kind);
}

static void test_mcdc_T05_case2_A0_C1_does_not_fire(void)
{
    mode_sm_result_t r = nominal_with(0, 1);
    TEST_ASSERT_EQUAL_INT(MODE_SM_STEP_IDLE, r.kind);
}

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_boot_nominal_runs_entry_actions_without_report);
    RUN_TEST(test_boot_safe_after_watchdog_reset);
    RUN_TEST(test_boot_in_degraded_is_refused_and_enters_safe);
    RUN_TEST(test_post_rejects_event_outside_the_model);
    RUN_TEST(test_step_with_nothing_pending_is_idle);
    RUN_TEST(test_T01_nominal_to_safe_on_ground);
    RUN_TEST(test_T02_nominal_to_safe_on_critical_fault);
    RUN_TEST(test_T03_nominal_to_degraded_on_ground);
    RUN_TEST(test_T04_nominal_to_degraded_on_noncritical_fault);
    RUN_TEST(test_T05_T06_inhibited_faults_in_nominal_are_refused_and_reported);
    RUN_TEST(test_T07_T08_degraded_to_safe);
    RUN_TEST(test_T09_degraded_to_nominal_on_ground_only);
    RUN_TEST(test_T10_inhibited_critical_fault_in_degraded_is_refused);
    RUN_TEST(test_T11_T12_safe_left_by_ground_with_exit_action);
    RUN_TEST(test_autonomous_events_never_leave_safe);
    RUN_TEST(test_command_for_current_mode_is_ignored_silently);
    RUN_TEST(test_ground_command_losing_to_priority_is_reported);
    RUN_TEST(test_D05_ignored_ground_command_is_reported_when_mode_changes);
    RUN_TEST(test_ignored_ground_command_stays_silent_on_internal_transition);
    RUN_TEST(test_D04_fault_latched_across_ground_deescalation);
    RUN_TEST(test_latched_fault_ignored_in_start_state_is_dropped_when_idle);
    RUN_TEST(test_coalescing_last_payload_wins);
    RUN_TEST(test_invalid_state_recovers_to_safe_keeping_latched_events);
    RUN_TEST(test_mcdc_T02_case1_A1_B1_fires);
    RUN_TEST(test_mcdc_T02_case2_A0_B1_does_not_fire);
    RUN_TEST(test_mcdc_T02_case3_A1_B0_does_not_fire);
    RUN_TEST(test_mcdc_T05_case1_A1_C1_fires);
    RUN_TEST(test_mcdc_T05_case2_A0_C1_does_not_fire);
    return UNITY_END();
}
