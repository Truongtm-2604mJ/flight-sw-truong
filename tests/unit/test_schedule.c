/* Unit suite: onboard time based schedule (schedule.c).
 *
 * The hand written mode manager tests of the original file are not
 * ported: modes.c is no longer in the flight build (Session 6). The
 * generated mode machine has its own suite, test_mode_sm.c, and the FDIR
 * inhibit now lives in the application (fdir6.c), which is tested at
 * integration and validation level.
 *
 * Ported to Unity from session-05/lab-5.3-safe-mode/tests/test_modes.c by tests/support/port_to_unity.py.
 */
#include <stddef.h>
#include <stdint.h>

#include "schedule.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

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
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(sched_insert(&s, 30, &c, 1)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(sched_insert(&s, 10, &a, 1)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(sched_insert(&s, 20, &b, 1)));
    rel_n = 0;
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(sched_release_due(&s, 5, on_release, NULL)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(2), (int64_t)(sched_release_due(&s, 25, on_release, NULL)));
    TEST_ASSERT_EQUAL_INT64((int64_t)('A'), (int64_t)(rel_first[0])); TEST_ASSERT_EQUAL_INT64((int64_t)('B'), (int64_t)(rel_first[1]));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(sched_count(&s)));
}

static void test_suspended_schedule_releases_nothing(void)
{
    sched_t s;
    uint8_t a = 'A';
    sched_init(&s);
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(sched_insert(&s, 10, &a, 1)));
    sched_set_enabled(&s, 0);                     /* entering SAFE */
    rel_n = 0;
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(sched_release_due(&s, 1000, on_release, NULL)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(rel_n));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(sched_count(&s)));                 /* kept, not lost */
    sched_set_enabled(&s, 1);                     /* ground decision */
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(sched_release_due(&s, 1000, on_release, NULL)));
}

static void test_schedule_limits_and_reset(void)
{
    sched_t s;
    uint8_t big[SCHED_MAX_TC + 1u] = { 0 };
    size_t i;
    sched_init(&s);
    TEST_ASSERT_EQUAL_INT64((int64_t)(-2), (int64_t)(sched_insert(&s, 1, big, 0)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(-2), (int64_t)(sched_insert(&s, 1, big, SCHED_MAX_TC + 1u)));
    for (i = 0; i < SCHED_MAX_ENTRIES; i++) {
        TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(sched_insert(&s, 1, big, SCHED_MAX_TC)));
    }
    TEST_ASSERT_EQUAL_INT64((int64_t)(-1), (int64_t)(sched_insert(&s, 1, big, 1)));
    sched_reset(&s);
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(sched_count(&s)));
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
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(sched_insert(&s, 1, &a, 1)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(0), (int64_t)(sched_insert(&s, 2, &a, 1)));
    rel_n = 0;
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(sched_release_due(&s, 10, disable_on_release, NULL)));
    TEST_ASSERT_EQUAL_INT64((int64_t)(1), (int64_t)(sched_count(&s)));
}

/* ---- Added in Session 7 after the first MC/DC measurement (gaps G-07, G-08) */

static void test_s7_insert_rejects_null_command(void)
{
    sched_t s;
    sched_init(&s);
    TEST_ASSERT_EQUAL_INT(-2, sched_insert(&s, 1, NULL, 1));
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)sched_count(&s));
}

static void test_s7_earliest_due_released_first_whatever_the_slot(void)
{
    sched_t s;
    uint8_t late = 'L', early = 'E', same = 'S';
    sched_init(&s);
    TEST_ASSERT_EQUAL_INT(0, sched_insert(&s, 20, &late, 1));   /* slot 0 */
    TEST_ASSERT_EQUAL_INT(0, sched_insert(&s, 10, &early, 1));  /* slot 1, earlier */
    TEST_ASSERT_EQUAL_INT(0, sched_insert(&s, 20, &same, 1));   /* slot 2, tie */
    rel_n = 0;
    TEST_ASSERT_EQUAL_INT(3, (int)sched_release_due(&s, 30, on_release, NULL));
    TEST_ASSERT_EQUAL_INT('E', rel_first[0]);
    TEST_ASSERT_EQUAL_INT('L', rel_first[1]);   /* tie: insertion order kept */
    TEST_ASSERT_EQUAL_INT('S', rel_first[2]);
}

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_schedule_releases_due_activities_in_time_order);
    RUN_TEST(test_suspended_schedule_releases_nothing);
    RUN_TEST(test_schedule_limits_and_reset);
    RUN_TEST(test_release_that_suspends_stops_the_pass);
    RUN_TEST(test_s7_insert_rejects_null_command);
    RUN_TEST(test_s7_earliest_due_released_first_whatever_the_slot);
    return UNITY_END();
}
