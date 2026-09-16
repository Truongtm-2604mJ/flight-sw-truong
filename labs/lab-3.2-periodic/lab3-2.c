#include <rtems.h>
#include <stdio.h>
#include <stdint.h>

/*
 * ============================================================
 * Lab 3.2 - Rate Monotonic Scheduling
 * Case 1: Nominal RMS priorities
 * ============================================================
 *
 * Tasks:
 *
 * ACS_CTRL : Period = 100 ms,  Workload = 20 ms
 * TM_HK    : Period = 500 ms,  Workload = 50 ms
 * PL_LOG   : Period = 1000 ms, Workload = 100 ms
 *
 * Utilization:
 *
 * U = 20/100 + 50/500 + 100/1000
 *   = 0.2 + 0.1 + 0.1
 *   = 0.4 = 40%
 *
 * Liu-Layland sufficient bound for n = 3:
 *
 * U <= 3 * (2^(1/3) - 1)
 *   ~= 0.7798 = 77.98%
 *
 * Therefore:
 *
 * 0.4 < 0.7798
 *
 * The task set is below the RMS sufficient bound.
 *
 * RTEMS:
 *
 * 1 tick = 10 ms
 */

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_TASKS       4
#define CONFIGURE_MAXIMUM_PERIODS     3

#define CONFIGURE_MICROSECONDS_PER_TICK 10000

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT

#include <rtems/confdefs.h>


/* ============================================================
 * Task priorities
 *
 * RTEMS:
 * smaller number = higher priority
 * ============================================================
 */

/*
 * ACS_CTRL has the shortest period (100 ms).
 * Therefore it gets the highest priority.
 */
#define ACS_CTRL_PRIORITY 30

/*
 * TM_HK has a 500 ms period.
 * It is longer than ACS_CTRL but shorter than PL_LOG.
 * Therefore it gets medium priority.
 */
#define TM_HK_PRIORITY 20

/*
 * PL_LOG has the longest period (1000 ms).
 * Therefore it gets the lowest priority.
 */
#define PL_LOG_PRIORITY 10


/* ============================================================
 * Periods
 *
 * 1 tick = 10 ms
 * ============================================================
 */

#define ACS_CTRL_PERIOD_TICKS  10     /* 100 ms  */
#define TM_HK_PERIOD_TICKS     50     /* 500 ms  */
#define PL_LOG_PERIOD_TICKS    100    /* 1000 ms */


/* ============================================================
 * Workload
 * ============================================================
 *
 * This function consumes CPU time.
 *
 * IMPORTANT:
 * The loop count is only an approximate workload.
 * It is NOT directly equal to milliseconds.
 *
 * We calibrate it empirically using RMS statistics.
 */

static void do_bounded_work(uint32_t iterations)
{
    volatile uint32_t i;
    volatile uint32_t dummy = 0;

    for (i = 0; i < iterations; ++i)
    {
        dummy += i;
    }

    (void)dummy;
}


/* ============================================================
 * ACS_CTRL
 * ============================================================
 */

static rtems_task acs_ctrl_task(rtems_task_argument arg)
{
    rtems_id period_id;
    rtems_status_code sc;

    (void)arg;

    sc = rtems_rate_monotonic_create(
        rtems_build_name('A', 'C', 'S', '1'),
        &period_id
    );

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf(
            "ACS_CTRL: rate monotonic create failed: %s\n",
            rtems_status_text(sc)
        );

        rtems_task_exit();
    }

    printf("ACS_CTRL started\n");

    while (1)
    {
        /*
         * Nominal workload = approximately 20 ms.
         */
        do_bounded_work(1000000);

        sc = rtems_rate_monotonic_period(
            period_id,
            ACS_CTRL_PERIOD_TICKS
        );

        if (sc == RTEMS_TIMEOUT)
        {
            printf("DEADLINE MISS: ACS_CTRL\n");
        }
        else if (sc != RTEMS_SUCCESSFUL)
        {
            printf(
                "ACS_CTRL period error: %s\n",
                rtems_status_text(sc)
            );
        }
    }
}


/* ============================================================
 * TM_HK
 * ============================================================
 */

static rtems_task tm_hk_task(rtems_task_argument arg)
{
    rtems_id period_id;
    rtems_status_code sc;

    (void)arg;

    sc = rtems_rate_monotonic_create(
        rtems_build_name('H', 'K', '1', '1'),
        &period_id
    );

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf(
            "TM_HK: rate monotonic create failed: %s\n",
            rtems_status_text(sc)
        );

        rtems_task_exit();
    }

    printf("TM_HK started\n");

    while (1)
    {
        /*
         * Nominal workload = approximately 50 ms.
         */
        do_bounded_work(2500);

        sc = rtems_rate_monotonic_period(
            period_id,
            TM_HK_PERIOD_TICKS
        );

        if (sc == RTEMS_TIMEOUT)
        {
            printf("DEADLINE MISS: TM_HK\n");
        }
        else if (sc != RTEMS_SUCCESSFUL)
        {
            printf(
                "TM_HK period error: %s\n",
                rtems_status_text(sc)
            );
        }
    }
}


/* ============================================================
 * PL_LOG
 * ============================================================
 */

static rtems_task pl_log_task(rtems_task_argument arg)
{
    rtems_id period_id;
    rtems_status_code sc;

    (void)arg;

    sc = rtems_rate_monotonic_create(
        rtems_build_name('L', 'O', 'G', '1'),
        &period_id
    );

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf(
            "PL_LOG: rate monotonic create failed: %s\n",
            rtems_status_text(sc)
        );

        rtems_task_exit();
    }

    printf("PL_LOG started\n");

    while (1)
    {
        /*
         * Nominal workload = approximately 100 ms.
         */
        do_bounded_work(50000);

        sc = rtems_rate_monotonic_period(
            period_id,
            PL_LOG_PERIOD_TICKS
        );

        if (sc == RTEMS_TIMEOUT)
        {
            printf("DEADLINE MISS: PL_LOG\n");
        }
        else if (sc != RTEMS_SUCCESSFUL)
        {
            printf(
                "PL_LOG period error: %s\n",
                rtems_status_text(sc)
            );
        }
    }
}


/* ============================================================
 * Init task
 * ============================================================
 */

rtems_task Init(rtems_task_argument arg)
{
    rtems_id task_id;
    rtems_status_code sc;

    (void)arg;

    printf("\n");
    printf("============================================\n");
    printf(" Lab 3.2 - Rate Monotonic Scheduling\n");
    printf("============================================\n");

    printf("Tick = 10 ms\n");
    printf("\n");

    printf("Task configuration:\n");
    printf("ACS_CTRL: T=100 ms, C=20 ms,  P=10\n");
    printf("TM_HK:    T=500 ms, C=50 ms,  P=20\n");
    printf("PL_LOG:   T=1000 ms, C=100 ms, P=30\n");
    printf("\n");

    printf("Utilization U = 0.40\n");
    printf("RM bound      = 0.7798\n");
    printf("U < bound     = YES\n");
    printf("\n");


    /* --------------------------------------------------------
     * ACS_CTRL
     * --------------------------------------------------------
     */

    sc = rtems_task_create(
        rtems_build_name('A', 'C', 'S', '1'),
        ACS_CTRL_PRIORITY,
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &task_id
    );

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf(
            "ERROR creating ACS_CTRL: %s\n",
            rtems_status_text(sc)
        );

        rtems_shutdown_executive(1);
    }

    sc = rtems_task_start(
        task_id,
        acs_ctrl_task,
        0
    );

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf(
            "ERROR starting ACS_CTRL: %s\n",
            rtems_status_text(sc)
        );

        rtems_shutdown_executive(1);
    }


    /* --------------------------------------------------------
     * TM_HK
     * --------------------------------------------------------
     */

    sc = rtems_task_create(
        rtems_build_name('H', 'K', '1', '1'),
        TM_HK_PRIORITY,
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &task_id
    );

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf(
            "ERROR creating TM_HK: %s\n",
            rtems_status_text(sc)
        );

        rtems_shutdown_executive(1);
    }

    sc = rtems_task_start(
        task_id,
        tm_hk_task,
        0
    );

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf(
            "ERROR starting TM_HK: %s\n",
            rtems_status_text(sc)
        );

        rtems_shutdown_executive(1);
    }


    /* --------------------------------------------------------
     * PL_LOG
     * --------------------------------------------------------
     */

    sc = rtems_task_create(
        rtems_build_name('L', 'O', 'G', '1'),
        PL_LOG_PRIORITY,
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &task_id
    );

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf(
            "ERROR creating PL_LOG: %s\n",
            rtems_status_text(sc)
        );

        rtems_shutdown_executive(1);
    }

    sc = rtems_task_start(
        task_id,
        pl_log_task,
        0
    );

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf(
            "ERROR starting PL_LOG: %s\n",
            rtems_status_text(sc)
        );

        rtems_shutdown_executive(1);
    }


    printf("\nAll periodic tasks started.\n");

    /*
     * Allow the system to run for approximately 10 seconds.
     *
     * 10 seconds / 10 ms per tick = 1000 ticks.
     */
    rtems_task_wake_after(1000);

    printf("\n");
    printf("============================================\n");
    printf(" Rate Monotonic Statistics - NOMINAL\n");
    printf("============================================\n");

    rtems_rate_monotonic_report_statistics();

    printf("\nSimulation finished.\n");

    rtems_shutdown_executive(0);
}