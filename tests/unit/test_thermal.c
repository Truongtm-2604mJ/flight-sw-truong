/* Unit suite: thermal recovery with bounded escalation (src/fdir/thermal.c).
 * Requirements FDIR-T1 (bounded attempts), FDIR-T2 (SAFE after the last),
 * FDIR-T3 (recovered: stop, stay DEGRADED). */
#include "thermal.h"
#include "unity.h"

static thermal_t t;

void setUp(void) { thermal_init(&t); }
void tearDown(void) { }

static void test_idle_without_alarm_or_outside_degraded(void)
{
    TEST_ASSERT_EQUAL_INT(THERM_NONE, thermal_step(&t, 0, 1, 0));
    TEST_ASSERT_EQUAL_INT(THERM_NONE, thermal_step(&t, 1, 0, 0));   /* NOMINAL/SAFE */
    TEST_ASSERT_EQUAL_UINT8(0, t.active);
}

static void test_three_attempts_then_exhausted(void)
{
    uint32_t now = 1000u;
    TEST_ASSERT_EQUAL_INT(THERM_ATTEMPT, thermal_step(&t, 1, 1, now));
    TEST_ASSERT_EQUAL_UINT8(1, t.attempts);
    TEST_ASSERT_EQUAL_INT(THERM_NONE, thermal_step(&t, 1, 1, now + THERM_PERIOD_MS - 1u));
    TEST_ASSERT_EQUAL_INT(THERM_ATTEMPT, thermal_step(&t, 1, 1, now + THERM_PERIOD_MS));
    TEST_ASSERT_EQUAL_UINT8(2, t.attempts);
    TEST_ASSERT_EQUAL_INT(THERM_ATTEMPT, thermal_step(&t, 1, 1, now + 2u * THERM_PERIOD_MS));
    TEST_ASSERT_EQUAL_UINT8(3, t.attempts);
    TEST_ASSERT_EQUAL_INT(THERM_EXHAUSTED, thermal_step(&t, 1, 1, now + 3u * THERM_PERIOD_MS));
    TEST_ASSERT_EQUAL_UINT8(0, t.active);
    TEST_ASSERT_EQUAL_UINT32(1, t.exhausted);
}

static void test_no_new_sequence_until_degraded_left(void)
{
    uint32_t now = 0u;
    int i;
    for (i = 0; i < 4; i++) {
        (void)thermal_step(&t, 1, 1, now);
        now += THERM_PERIOD_MS;
    }
    TEST_ASSERT_EQUAL_UINT32(1, t.exhausted);
    /* SAFE is applied at the next 2 s mode cycle: still DEGRADED meanwhile. */
    TEST_ASSERT_EQUAL_INT(THERM_NONE, thermal_step(&t, 1, 1, now + 500u));
    TEST_ASSERT_EQUAL_INT(THERM_NONE, thermal_step(&t, 1, 1, now + 1500u));
    TEST_ASSERT_EQUAL_UINT32(1, t.sequences);
    /* Left DEGRADED (SAFE), back later: a new episode may try again. */
    TEST_ASSERT_EQUAL_INT(THERM_NONE, thermal_step(&t, 1, 0, now + 2000u));
    TEST_ASSERT_EQUAL_INT(THERM_ATTEMPT, thermal_step(&t, 1, 1, now + 60000u));
    TEST_ASSERT_EQUAL_UINT32(2, t.sequences);
}

static void test_recovered_stops_and_reports(void)
{
    TEST_ASSERT_EQUAL_INT(THERM_ATTEMPT, thermal_step(&t, 1, 1, 0));
    TEST_ASSERT_EQUAL_INT(THERM_ATTEMPT, thermal_step(&t, 1, 1, THERM_PERIOD_MS));
    TEST_ASSERT_EQUAL_INT(THERM_RECOVERED, thermal_step(&t, 0, 1, THERM_PERIOD_MS + 500u));
    TEST_ASSERT_EQUAL_UINT8(0, t.active);
    TEST_ASSERT_EQUAL_UINT32(0, t.exhausted);
    /* A later alarm in the same DEGRADED episode starts a new sequence. */
    TEST_ASSERT_EQUAL_INT(THERM_ATTEMPT, thermal_step(&t, 1, 1, 50000u));
}

static void test_mode_change_stands_down_silently(void)
{
    TEST_ASSERT_EQUAL_INT(THERM_ATTEMPT, thermal_step(&t, 1, 1, 0));
    TEST_ASSERT_EQUAL_INT(THERM_NONE, thermal_step(&t, 1, 0, 100u));   /* ground: NOMINAL */
    TEST_ASSERT_EQUAL_UINT8(0, t.active);
    TEST_ASSERT_EQUAL_UINT32(0, t.exhausted);
}

static void test_time_wraps(void)
{
    uint32_t now = 0xFFFFFFFFu - 5000u;
    TEST_ASSERT_EQUAL_INT(THERM_ATTEMPT, thermal_step(&t, 1, 1, now));
    TEST_ASSERT_EQUAL_INT(THERM_NONE, thermal_step(&t, 1, 1, now + 4000u));
    TEST_ASSERT_EQUAL_INT(THERM_ATTEMPT, thermal_step(&t, 1, 1, now + THERM_PERIOD_MS));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_idle_without_alarm_or_outside_degraded);
    RUN_TEST(test_three_attempts_then_exhausted);
    RUN_TEST(test_no_new_sequence_until_degraded_left);
    RUN_TEST(test_recovered_stops_and_reports);
    RUN_TEST(test_mode_change_stands_down_silently);
    RUN_TEST(test_time_wraps);
    return UNITY_END();
}
