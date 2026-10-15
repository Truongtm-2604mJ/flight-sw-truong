/*
 * Lab 5.1 - Monitoring tests. Build and run: make test
 *
 * The first five tests are the ones the session asks for, in order.
 * Test limits: low -100 / clear -90, high clear 90 / high 100.
 */
#include "../monitor.h"
#include "test_util.h"

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
    CHECK_EQ(feed(50, 5), 0);
    CHECK_EQ(mon_update(&m, 150), MON_NO_CHANGE);   /* one spike */
    CHECK_EQ(m.state, MON_WARN);                     /* noted, not raised */
    CHECK_EQ(feed(50, 5), 0);
    CHECK_EQ(m.state, MON_OK);
    CHECK_EQ(m.filtered, 1);                         /* rejected transient */
}

static void test_three_consecutive_samples_raise_exactly_one_event(void)
{
    setup(3);
    CHECK_EQ(mon_update(&m, 150), MON_NO_CHANGE);
    CHECK_EQ(mon_update(&m, 150), MON_NO_CHANGE);
    CHECK_EQ(mon_update(&m, 150), MON_RAISED_HIGH);  /* on the 3rd */
    CHECK_EQ(m.state, MON_ALARM);
    CHECK_EQ(m.side, MON_SIDE_HIGH);
    CHECK_EQ(m.filtered, 0);
}

static void test_twenty_further_samples_raise_no_more_events(void)
{
    setup(3);
    CHECK_EQ(feed(150, 3), 1);
    CHECK_EQ(feed(150, 20), 0);
    CHECK_EQ(feed(1000, 20), 0);                     /* even further out */
    CHECK_EQ(m.state, MON_ALARM);
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
    CHECK_EQ(events, 0);
    CHECK(m.state != MON_ALARM);

    /* From ALARM: a value sitting in the hysteresis band, or touching
     * the limit, never clears. */
    setup(3);
    CHECK_EQ(feed(101, 3), 1);
    events = 0;
    for (i = 0; i < 100; i++) {
        events += (mon_update(&m, 95) != MON_NO_CHANGE);   /* in the band  */
        events += (mon_update(&m, 100) != MON_NO_CHANGE);  /* on the limit */
        events += (mon_update(&m, 91) != MON_NO_CHANGE);   /* just above clear */
    }
    CHECK_EQ(events, 0);
    CHECK_EQ(m.state, MON_ALARM);

    /* Swinging across the whole band, alarm to clear and back every
     * sample, is also churn free: neither side ever persists. */
    events = 0;
    for (i = 0; i < 100; i++) {
        events += (mon_update(&m, 80) != MON_NO_CHANGE);
        events += (mon_update(&m, 120) != MON_NO_CHANGE);
    }
    CHECK_EQ(events, 0);
    CHECK_EQ(m.state, MON_ALARM);
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
    CHECK_EQ(events, 0);
    CHECK_EQ(m.state, MON_OK);
    CHECK_EQ(m.violation_count, 0);

    /* Disabling while in ALARM forgets it silently; re-enabling starts
     * from OK and needs the full persistence again. */
    setup(3);
    CHECK_EQ(feed(150, 3), 1);
    mon_set_enabled(&m, 0);
    CHECK_EQ(m.state, MON_OK);
    mon_set_enabled(&m, 1);
    CHECK_EQ(feed(150, 2), 0);
    CHECK_EQ(feed(150, 1), 1);
}

/* ---- Further behaviour -------------------------------------------------- */

static void test_counter_resets_on_any_in_limit_sample(void)
{
    int i;
    setup(3);
    for (i = 0; i < 50; i++) {
        CHECK_EQ(mon_update(&m, 150), MON_NO_CHANGE);
        CHECK_EQ(mon_update(&m, 150), MON_NO_CHANGE);
        CHECK_EQ(mon_update(&m, 0), MON_NO_CHANGE);   /* resets to 0 */
    }
    CHECK_EQ(m.state, MON_OK);
    CHECK_EQ(m.filtered, 50);
}

static void test_clearing_needs_persistence_inside_clear_threshold(void)
{
    setup(3);
    CHECK_EQ(feed(150, 3), 1);
    CHECK_EQ(mon_update(&m, 90), MON_NO_CHANGE);      /* clear, 1 */
    CHECK_EQ(mon_update(&m, 90), MON_NO_CHANGE);      /* clear, 2 */
    CHECK_EQ(mon_update(&m, 95), MON_NO_CHANGE);      /* band: restarts */
    CHECK_EQ(mon_update(&m, 0), MON_NO_CHANGE);
    CHECK_EQ(mon_update(&m, 0), MON_NO_CHANGE);
    CHECK_EQ(mon_update(&m, 0), MON_CLEARED);         /* 3 in a row */
    CHECK_EQ(m.state, MON_OK);
    CHECK_EQ(m.side, MON_SIDE_NONE);
    /* A second, separate excursion gives a second event. */
    CHECK_EQ(feed(150, 3), 1);
}

static void test_low_limit_and_side_switching(void)
{
    setup(3);
    CHECK_EQ(mon_update(&m, -150), MON_NO_CHANGE);
    CHECK_EQ(mon_update(&m, -150), MON_NO_CHANGE);
    CHECK_EQ(mon_update(&m, -150), MON_RAISED_LOW);
    CHECK_EQ(m.side, MON_SIDE_LOW);
    CHECK_EQ(feed(-85, 3), 1);                        /* clears at -85 */
    CHECK_EQ(m.state, MON_OK);

    /* Two violations high then one low is not three of anything. */
    setup(3);
    CHECK_EQ(mon_update(&m, 150), MON_NO_CHANGE);
    CHECK_EQ(mon_update(&m, 150), MON_NO_CHANGE);
    CHECK_EQ(mon_update(&m, -150), MON_NO_CHANGE);
    CHECK_EQ(m.violation_count, 1);
    CHECK_EQ(m.filtered, 1);
}

static void test_persistence_of_one_reacts_immediately(void)
{
    setup(1);
    CHECK_EQ(mon_update(&m, 101), MON_RAISED_HIGH);
    CHECK_EQ(mon_update(&m, 90), MON_CLEARED);
}

static void test_definition_check(void)
{
    setup(3);
    CHECK_EQ(mon_check_definition(&m), 0);
    m.persistence = 0;
    CHECK_EQ(mon_check_definition(&m), -1);
    setup(3); m.limit_high_clear = 100;             /* clear == alarm */
    CHECK_EQ(mon_check_definition(&m), -1);
    setup(3); m.limit_low_clear = -100;
    CHECK_EQ(mon_check_definition(&m), -1);
    setup(3); m.limit_low_clear = 95;               /* clear bands cross */
    CHECK_EQ(mon_check_definition(&m), -1);
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
    CHECK(mon_find(t, 3, 2) == &t[1]);
    CHECK(mon_find(t, 3, 9) == NULL);

    values[1] = 0; values[2] = 0; values[3] = 500;   /* param 3 out */
    calls = 0;
    for (i = 0; i < 4; i++) {
        mon_table_run(t, 3, get_value, on_tr, NULL);
    }
    CHECK_EQ(calls, 0);                               /* persistence 5 */
    mon_table_run(t, 3, get_value, on_tr, NULL);
    CHECK_EQ(calls, 1);
    CHECK_EQ(last_param, 3);
    CHECK_EQ(last_tr, MON_RAISED_HIGH);

    /* A disabled entry is skipped entirely. */
    mon_set_enabled(&t[0], 0);
    values[1] = -500;
    for (i = 0; i < 10; i++) {
        mon_table_run(t, 3, get_value, on_tr, NULL);
    }
    CHECK_EQ(calls, 1);
}

int main(void)
{
    printf("Lab 5.1 - parameter monitoring tests\n\n");
    RUN(test_single_out_of_limit_sample_raises_nothing);
    RUN(test_three_consecutive_samples_raise_exactly_one_event);
    RUN(test_twenty_further_samples_raise_no_more_events);
    RUN(test_oscillation_between_alarm_and_clear_has_no_churn);
    RUN(test_disabled_monitor_produces_no_events);
    RUN(test_counter_resets_on_any_in_limit_sample);
    RUN(test_clearing_needs_persistence_inside_clear_threshold);
    RUN(test_low_limit_and_side_switching);
    RUN(test_persistence_of_one_reacts_immediately);
    RUN(test_definition_check);
    RUN(test_table_run_reports_the_right_parameter);
    return TEST_SUMMARY();
}
