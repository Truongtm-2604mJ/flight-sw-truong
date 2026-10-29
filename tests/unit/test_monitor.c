/* Unit suite: parameter monitoring, persistence and hysteresis (monitor.c).
 *
 * Ported to Unity from session-05/lab-5.1-monitoring/tests/test_monitor.c by tests/support/port_to_unity.py.
 */
/*
 * Lab 5.1 - Monitoring tests. Build and run: make test
 *
 * The first five tests are the ones the session asks for, in order.
 * Test limits: low -100 / clear -90, high clear 90 / high 100.
 */
#include "monitor.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

#define EVT 0x0220u

static monitor_entry_t m;

static void setup(uint16_t persistence)
{
    mon_define(&m, 2, -100, -90, 90, 100, persistence, EVT, 2);
}

/* Feed n identical samples; return how many transitions they caused. */
static int feed(int32_t value, int n)
{
    int events = 0;
    int i;
    for (i = 0; i < n; i++) {
        if (mon_update(&m, value) != MON_NO_CHANGE) {
            events++;
        }
    }
    return events;
}

/* ---- The five required tests ------------------------------------------ */

static void test_single_out_of_limit_sample_raises_nothing(void)
{
    setup(3);
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(feed(50, 5)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, 150)));   /* one spike */
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_WARN), (int64_t)(m.state));                     /* noted, not raised */
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(feed(50, 5)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_OK), (int64_t)(m.state));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(m.filtered));                         /* rejected transient */
}

static void test_three_consecutive_samples_raise_exactly_one_event(void)
{
    setup(3);
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, 150)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, 150)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_RAISED_HIGH), (int64_t)(mon_update(&m, 150)));  /* on the 3rd */
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_ALARM), (int64_t)(m.state));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_SIDE_HIGH), (int64_t)(m.side));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(m.filtered));
}

static void test_twenty_further_samples_raise_no_more_events(void)
{
    setup(3);
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(feed(150, 3)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(feed(150, 20)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(feed(1000, 20)));                     /* even further out */
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_ALARM), (int64_t)(m.state));
}

static void test_oscillation_between_alarm_and_clear_has_no_churn(void)
{
    int i, events = 0;

    /* From OK: alternate just above the alarm limit and just above the
     * clear threshold. Never three violations in a row. */
    setup(3);
    for (i = 0; i < 100; i++) {
        events += (mon_update(&m, 101) != MON_NO_CHANGE);
        events += (mon_update(&m, 91) != MON_NO_CHANGE);
    }
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(events));
    TEST_ASSERT_TRUE(m.state != MON_ALARM);

    /* From ALARM: a value sitting in the hysteresis band, or touching
     * the limit, never clears. */
    setup(3);
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(feed(101, 3)));
    events = 0;
    for (i = 0; i < 100; i++) {
        events += (mon_update(&m, 95) != MON_NO_CHANGE);   /* in the band  */
        events += (mon_update(&m, 100) != MON_NO_CHANGE);  /* on the limit */
        events += (mon_update(&m, 91) != MON_NO_CHANGE);   /* just above clear */
    }
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(events));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_ALARM), (int64_t)(m.state));

    /* Swinging across the whole band, alarm to clear and back every
     * sample, is also churn free: neither side ever persists. */
    events = 0;
    for (i = 0; i < 100; i++) {
        events += (mon_update(&m, 80) != MON_NO_CHANGE);
        events += (mon_update(&m, 120) != MON_NO_CHANGE);
    }
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(events));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_ALARM), (int64_t)(m.state));
}

static void test_disabled_monitor_produces_no_events(void)
{
    int i, events = 0;

    setup(3);
    mon_set_enabled(&m, 0);
    for (i = 0; i < 50; i++) {
        events += (mon_update(&m, 10000) != MON_NO_CHANGE);
        events += (mon_update(&m, -10000) != MON_NO_CHANGE);
    }
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(events));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_OK), (int64_t)(m.state));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(m.violation_count));

    /* Disabling while in ALARM forgets it silently; re-enabling starts
     * from OK and needs the full persistence again. */
    setup(3);
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(feed(150, 3)));
    mon_set_enabled(&m, 0);
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_OK), (int64_t)(m.state));
    mon_set_enabled(&m, 1);
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(feed(150, 2)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(feed(150, 1)));
}

/* ---- Further behaviour -------------------------------------------------- */

static void test_counter_resets_on_any_in_limit_sample(void)
{
    int i;
    setup(3);
    for (i = 0; i < 50; i++) {
        TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, 150)));
        TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, 150)));
        TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, 0)));   /* resets to 0 */
    }
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_OK), (int64_t)(m.state));
    TEST_ASSERT_EQUAL_INT64((int64_t)(50), (int64_t)(m.filtered));
}

static void test_clearing_needs_persistence_inside_clear_threshold(void)
{
    setup(3);
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(feed(150, 3)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, 90)));      /* clear, 1 */
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, 90)));      /* clear, 2 */
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, 95)));      /* band: restarts */
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, 0)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, 0)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_CLEARED), (int64_t)(mon_update(&m, 0)));         /* 3 in a row */
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_OK), (int64_t)(m.state));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_SIDE_NONE), (int64_t)(m.side));
    /* A second, separate excursion gives a second event. */
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(feed(150, 3)));
}

static void test_low_limit_and_side_switching(void)
{
    setup(3);
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, -150)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, -150)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_RAISED_LOW), (int64_t)(mon_update(&m, -150)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_SIDE_LOW), (int64_t)(m.side));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(feed(-85, 3)));                        /* clears at -85 */
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_OK), (int64_t)(m.state));

    /* Two violations high then one low is not three of anything. */
    setup(3);
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, 150)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, 150)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_NO_CHANGE), (int64_t)(mon_update(&m, -150)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(m.violation_count));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(m.filtered));
}

static void test_persistence_of_one_reacts_immediately(void)
{
    setup(1);
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_RAISED_HIGH), (int64_t)(mon_update(&m, 101)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_CLEARED), (int64_t)(mon_update(&m, 90)));
}

static void test_definition_check(void)
{
    setup(3);
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(mon_check_definition(&m)));
    m.persistence = 0;
    TEST_ASSERT_EQUAL_INT64((int64_t)(-1), (int64_t)(mon_check_definition(&m)));
    setup(3); m.limit_high_clear = 100;             /* clear == alarm */
    TEST_ASSERT_EQUAL_INT64((int64_t)(-1), (int64_t)(mon_check_definition(&m)));
    setup(3); m.limit_low_clear = -100;
    TEST_ASSERT_EQUAL_INT64((int64_t)(-1), (int64_t)(mon_check_definition(&m)));
    setup(3); m.limit_low_clear = 95;               /* clear bands cross */
    TEST_ASSERT_EQUAL_INT64((int64_t)(-1), (int64_t)(mon_check_definition(&m)));
}

/* ---- Table driven use -------------------------------------------------- */

static int32_t values[4];
static int calls;
static uint16_t last_param;
static mon_transition_t last_tr;

static int32_t get_value(uint16_t param_id, void *ctx)
{
    (void)ctx;
    return values[param_id];
}

static void on_tr(const monitor_entry_t *e, mon_transition_t tr,
                  int32_t value, void *ctx)
{
    (void)value; (void)ctx;
    calls++;
    last_param = e->param_id;
    last_tr = tr;
}

static void test_table_run_reports_the_right_parameter(void)
{
    monitor_entry_t t[3];
    int i;

    mon_define(&t[0], 1, -100, -90, 90, 100, 3, 0x210, 4);
    mon_define(&t[1], 2, -100, -90, 90, 100, 3, 0x220, 2);
    mon_define(&t[2], 3, -100, -90, 90, 100, 5, 0x230, 2);
    TEST_ASSERT_TRUE(mon_find(t, 3, 2) == &t[1]);
    TEST_ASSERT_TRUE(mon_find(t, 3, 9) == NULL);

    values[1] = 0; values[2] = 0; values[3] = 500;   /* param 3 out */
    calls = 0;
    for (i = 0; i < 4; i++) {
        mon_table_run(t, 3, get_value, on_tr, NULL);
    }
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(calls));                               /* persistence 5 */
    mon_table_run(t, 3, get_value, on_tr, NULL);
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(calls));
    TEST_ASSERT_EQUAL_INT64((int64_t)(3), (int64_t)(last_param));
    TEST_ASSERT_EQUAL_INT64((int64_t)(MON_RAISED_HIGH), (int64_t)(last_tr));

    /* A disabled entry is skipped entirely. */
    mon_set_enabled(&t[0], 0);
    values[1] = -500;
    for (i = 0; i < 10; i++) {
        mon_table_run(t, 3, get_value, on_tr, NULL);
    }
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(calls));
}

/* ---- MC/DC (labs/session-07/coverage/mcdc.md) ---------------------------
 * MC-1  monitor.c, in ALARM:  value >= limit_low_clear && value <= limit_high_clear
 *       A = above the low clear threshold, B = below the high clear threshold.
 *       Outcome observed: clear_count advances (true) or restarts (false).
 *       Limits: low -100 / clear -90, high clear 90 / high 100.             */

static void alarm_high_then(int32_t first_clear_sample)
{
    setup(2);
    (void)feed(150, 2);                         /* ALARM, high side */
    TEST_ASSERT_EQUAL_INT(MON_ALARM, m.state);
    (void)mon_update(&m, first_clear_sample);
}

static void test_mcdc_MC1_case1_A1_B1_in_band_counts(void)
{
    alarm_high_then(0);                         /* -90 <= 0 <= 90 */
    TEST_ASSERT_EQUAL_UINT16(1u, m.clear_count);
}

static void test_mcdc_MC1_case2_A0_B1_below_low_clear_restarts(void)
{
    alarm_high_then(-95);                       /* A false: -95 < -90 */
    TEST_ASSERT_EQUAL_UINT16(0u, m.clear_count);
    TEST_ASSERT_EQUAL_INT(MON_ALARM, m.state);
}

static void test_mcdc_MC1_case3_A1_B0_above_high_clear_restarts(void)
{
    alarm_high_then(95);                        /* B false: 95 > 90 */
    TEST_ASSERT_EQUAL_UINT16(0u, m.clear_count);
    TEST_ASSERT_EQUAL_INT(MON_ALARM, m.state);
}

/* MC-2  monitor.c, OK/WARN with a violation:  side != e->side && violation_count > 0
 *       A = the violated limit differs from the previous one,
 *       B = a run of violations is in progress.
 *       Outcome observed: `filtered` counts the abandoned run (true).     */

static void test_mcdc_MC2_case1_A1_B1_flip_counts_abandoned_run(void)
{
    setup(3);
    (void)mon_update(&m, 150);                  /* run of 1 on the high side */
    (void)mon_update(&m, -150);                 /* flips to the low side */
    TEST_ASSERT_EQUAL_UINT32(1u, m.filtered);
    TEST_ASSERT_EQUAL_UINT16(1u, m.violation_count);
    TEST_ASSERT_EQUAL_INT(MON_SIDE_LOW, m.side);
}

static void test_mcdc_MC2_case2_A0_B1_same_side_continues_run(void)
{
    setup(3);
    (void)mon_update(&m, 150);
    (void)mon_update(&m, 150);                  /* same side */
    TEST_ASSERT_EQUAL_UINT32(0u, m.filtered);
    TEST_ASSERT_EQUAL_UINT16(2u, m.violation_count);
}

static void test_mcdc_MC2_case3_A1_B0_first_violation_starts_run(void)
{
    setup(3);
    (void)mon_update(&m, 0);                    /* in limits: side NONE, count 0 */
    (void)mon_update(&m, -150);                 /* side differs, no run yet */
    TEST_ASSERT_EQUAL_UINT32(0u, m.filtered);
    TEST_ASSERT_EQUAL_UINT16(1u, m.violation_count);
}

/* ---- Added in Session 7 after the first MC/DC measurement (gap G-05) ---- */

static int32_t const_150(uint16_t param_id, void *c)
{
    (void)param_id; (void)c;
    return 150;
}

static void test_s7_table_run_without_callback_still_monitors(void)
{
    monitor_entry_t table[1];
    int k;
    mon_define(&table[0], 2, -100, -90, 90, 100, 2, EVT, 2);
    for (k = 0; k < 2; k++) {
        mon_table_run(table, 1, const_150, NULL, NULL);  /* no callback */
    }
    TEST_ASSERT_EQUAL_INT(MON_ALARM, table[0].state);
}

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_single_out_of_limit_sample_raises_nothing);
    RUN_TEST(test_three_consecutive_samples_raise_exactly_one_event);
    RUN_TEST(test_twenty_further_samples_raise_no_more_events);
    RUN_TEST(test_oscillation_between_alarm_and_clear_has_no_churn);
    RUN_TEST(test_disabled_monitor_produces_no_events);
    RUN_TEST(test_counter_resets_on_any_in_limit_sample);
    RUN_TEST(test_clearing_needs_persistence_inside_clear_threshold);
    RUN_TEST(test_low_limit_and_side_switching);
    RUN_TEST(test_persistence_of_one_reacts_immediately);
    RUN_TEST(test_definition_check);
    RUN_TEST(test_table_run_reports_the_right_parameter);
    RUN_TEST(test_mcdc_MC1_case1_A1_B1_in_band_counts);
    RUN_TEST(test_mcdc_MC1_case2_A0_B1_below_low_clear_restarts);
    RUN_TEST(test_mcdc_MC1_case3_A1_B0_above_high_clear_restarts);
    RUN_TEST(test_mcdc_MC2_case1_A1_B1_flip_counts_abandoned_run);
    RUN_TEST(test_mcdc_MC2_case2_A0_B1_same_side_continues_run);
    RUN_TEST(test_mcdc_MC2_case3_A1_B0_first_violation_starts_run);
    RUN_TEST(test_s7_table_run_without_callback_still_monitors);
    return UNITY_END();
}
