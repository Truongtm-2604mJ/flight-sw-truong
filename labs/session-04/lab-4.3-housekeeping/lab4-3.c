#include <rtems.h>
#include <stdio.h>
#include <stdint.h>

#include "tmtc_tasks.h"
#include "hk.h"

/*
 * ============================================================
 * Lab 4.3 - Periodic housekeeping and event reporting
 * ============================================================
 *
 * The Lab 3.2 rate monotonic task set, with TM_HK now doing real
 * work, plus the two TM/TC tasks from Lab 4.2 running in the slack.
 *
 * Periodic tasks (rate monotonic priorities, unchanged from Lab 3.2):
 *
 * ACS_CTRL : Period = 100 ms,  Workload = 20 ms          P = 10
 * TM_HK    : Period = 500 ms,  Workload = 50 ms + TM work P = 20
 * PL_LOG   : Period = 1000 ms, Workload = 100 ms         P = 30
 *
 * Aperiodic tasks (below every periodic task):
 *
 * TC_RX    : command reception and dispatch              P = 40
 * TM_TX    : downlink of queued TM packets               P = 50
 *
 * TM_HK each period:
 *   1. sample the parameters and timestamp them
 *   2. run the battery temperature monitor (event on a crossing)
 *   3. if enabled: pack TM(3,25), CRC, queue for downlink
 *   4. the Lab 3.2 workload (stands for the acquisition cost)
 *
 * Utilization and schedulability: see docs/tmtc-design.md, section 5.
 *
 * RTEMS:
 *
 * 1 tick = 10 ms
 */

/*
 * RUN_SECONDS > 0 : run for that long, print the rate monotonic
 *                   statistics and shut down (timing measurement).
 * RUN_SECONDS = 0 : run until the simulator is stopped (ground demo).
 */
#ifndef RUN_SECONDS
#define RUN_SECONDS 0
#endif

#define HK_TASK_STACK (8u * 1024u)

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_TASKS            (1 + 3 + TMTC_TASK_COUNT)
#define CONFIGURE_MAXIMUM_PERIODS          3
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES   1
#define CONFIGURE_MESSAGE_BUFFER_MEMORY \
    CONFIGURE_MESSAGE_BUFFERS_FOR_QUEUE(TM_QUEUE_DEPTH, PUS_MAX_PACKET)

#define CONFIGURE_EXTRA_TASK_STACKS \
    (TMTC_TASK_COUNT * TMTC_TASK_STACK + HK_TASK_STACK)

#define CONFIGURE_MICROSECONDS_PER_TICK 10000

#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INIT_TASK_STACK_SIZE (16 * 1024)

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT

#include <rtems/confdefs.h>


/* ============================================================
 * Task priorities (RTEMS: smaller number = higher priority)
 * ============================================================
 */

#define ACS_CTRL_PRIORITY 10
#define TM_HK_PRIORITY    20
#define PL_LOG_PRIORITY   30

/* TC_RX_PRIORITY 40 and TM_TX_PRIORITY 50 are in tmtc_tasks.h. */


/* ============================================================
 * Periods (1 tick = 10 ms)
 * ============================================================
 */

#define ACS_CTRL_PERIOD_TICKS  10     /* 100 ms  */
#define TM_HK_PERIOD_TICKS     50     /* 500 ms  */
#define PL_LOG_PERIOD_TICKS    100    /* 1000 ms */


/* ============================================================
 * Workload
 * ============================================================
 *
 * Iteration counts calibrated on SIS in Lab 3.2 (about 2 170
 * iterations per ms). They are NOT milliseconds and must be
 * recalibrated on any other simulator or target.
 *
 * Note: labs/lab-3.2-periodic/lab3-2.c as committed has 195000 for
 * ACS_CTRL; labs/docs/timing-analysis.md states the calibrated value
 * 43000 (20 ms). The calibrated value is used here.
 */

#define ACS_CTRL_WORK_ITERATIONS  43000u    /* ~20 ms  */
#define TM_HK_WORK_ITERATIONS     108000u   /* ~50 ms  */
#define PL_LOG_WORK_ITERATIONS    217000u   /* ~100 ms */

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
 * Shared state
 * ============================================================
 */

static hk_ctx_t hk;

/*
 * Deadline miss counters. Tasks do not print: the console belongs to
 * the downlink task. Init reports these at the end of a timed run.
 */
static volatile uint32_t acs_ctrl_misses;
static volatile uint32_t tm_hk_misses;
static volatile uint32_t pl_log_misses;


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
        rtems_task_exit();
    }

    while (1)
    {
        sc = rtems_rate_monotonic_period(
            period_id,
            ACS_CTRL_PERIOD_TICKS
        );

        if (sc == RTEMS_TIMEOUT)
        {
            acs_ctrl_misses = acs_ctrl_misses + 1;
        }

        do_bounded_work(ACS_CTRL_WORK_ITERATIONS);
    }
}


/* ============================================================
 * TM_HK - the housekeeping generator
 * ============================================================
 *
 * Same rate monotonic period object as Lab 3.2. No new timing
 * mechanism: the period IS the housekeeping collection interval.
 */

static rtems_task tm_hk_task(rtems_task_argument arg)
{
    rtems_id period_id;
    rtems_status_code sc;
    uint64_t t_start;
    uint64_t elapsed_us;

    (void)arg;

    sc = rtems_rate_monotonic_create(
        rtems_build_name('H', 'K', '1', '1'),
        &period_id
    );

    if (sc != RTEMS_SUCCESSFUL)
    {
        rtems_task_exit();
    }

    while (1)
    {
        /*
         * Period call at the TOP of the loop (the first call starts
         * the period and returns at once). Lab 3.2 had it at the
         * bottom, which runs the first two jobs back to back; for a
         * housekeeping generator that means two reports with the same
         * timestamp at start up, so the order is changed here.
         */
        sc = rtems_rate_monotonic_period(
            period_id,
            TM_HK_PERIOD_TICKS
        );

        if (sc == RTEMS_TIMEOUT)
        {
            tm_hk_misses = tm_hk_misses + 1;
        }

        /*
         * New in Lab 4.3: sample, monitor, pack, checksum, queue.
         *
         * Done first so that sampling happens at the release of the
         * period, with as little jitter as possible. The time spent
         * here is measured and downlinked in the NEXT report as the
         * HK_EXEC parameter. It is wall clock time, so a value near
         * 20 ms means ACS_CTRL preempted this section; the minimum
         * over a run is the uninterrupted cost.
         */
        t_start = rtems_clock_get_uptime_nanoseconds();

        hk_cycle(&hk);

        elapsed_us =
            (rtems_clock_get_uptime_nanoseconds() - t_start) / 1000u;
        hk.last_exec_us =
            (elapsed_us > 65535u) ? 65535u : (uint16_t)elapsed_us;

        /*
         * Lab 3.2 workload, approximately 50 ms. Stands for the cost
         * of acquiring the parameters from real units.
         */
        do_bounded_work(TM_HK_WORK_ITERATIONS);
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
        rtems_task_exit();
    }

    while (1)
    {
        sc = rtems_rate_monotonic_period(
            period_id,
            PL_LOG_PERIOD_TICKS
        );

        if (sc == RTEMS_TIMEOUT)
        {
            pl_log_misses = pl_log_misses + 1;
        }

        do_bounded_work(PL_LOG_WORK_ITERATIONS);
    }
}


/* ============================================================
 * Init task
 * ============================================================
 */

static void start_periodic_task(
    rtems_name name,
    rtems_task_priority priority,
    size_t stack_size,
    rtems_task_entry entry
)
{
    rtems_id task_id;
    rtems_status_code sc;

    sc = rtems_task_create(
        name,
        priority,
        stack_size,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &task_id
    );

    if (sc == RTEMS_SUCCESSFUL)
    {
        sc = rtems_task_start(task_id, entry, 0);
    }

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf("ERROR starting periodic task: %s\n", rtems_status_text(sc));
        rtems_shutdown_executive(1);
    }
}

rtems_task Init(rtems_task_argument arg)
{
    rtems_status_code sc;

    (void)arg;

    printf("\n");
    printf("============================================\n");
    printf(" Lab 4.3 - Housekeeping and event reporting\n");
    printf("============================================\n");
    printf("Tick = 10 ms\n");
    printf("\n");
    printf("ACS_CTRL: T=100 ms,  P=%d\n", ACS_CTRL_PRIORITY);
    printf("TM_HK:    T=500 ms,  P=%d  TM(3,25) on APID 0x%03X\n",
           TM_HK_PRIORITY, (unsigned)APID_HK);
    printf("PL_LOG:   T=1000 ms, P=%d\n", PL_LOG_PRIORITY);
    printf("TC_RX:    aperiodic, P=%d  TC on APID 0x%03X\n",
           TC_RX_PRIORITY, (unsigned)APID_CMD);
    printf("TM_TX:    aperiodic, P=%d\n", TM_TX_PRIORITY);
    printf("T_BAT limit %d.%02d C, hysteresis %d.%02d C, events on APID 0x%03X\n",
           TBAT_LIMIT_CDEG / 100, TBAT_LIMIT_CDEG % 100,
           TBAT_HYSTERESIS_CDEG / 100, TBAT_HYSTERESIS_CDEG % 100,
           (unsigned)APID_EVENT);
    printf("\n");
    fflush(stdout);

    /*
     * TM/TC chain first, then the housekeeping service on top of it.
     */
    sc = tmtc_init();

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf("ERROR tmtc_init: %s\n", rtems_status_text(sc));
        rtems_shutdown_executive(1);
    }

    hk_init(&hk, tmtc_tm_sink, NULL, tmtc_now, tmtc_tc_ctx(),
            tmtc_tm_dropped);

    if (hk_register_service(&hk, tmtc_tc_ctx()) != 0)
    {
        printf("ERROR registering service 3\n");
        rtems_shutdown_executive(1);
    }

    start_periodic_task(
        rtems_build_name('A', 'C', 'S', '1'),
        ACS_CTRL_PRIORITY,
        RTEMS_MINIMUM_STACK_SIZE,
        acs_ctrl_task
    );

    start_periodic_task(
        rtems_build_name('H', 'K', '1', '1'),
        TM_HK_PRIORITY,
        HK_TASK_STACK,
        tm_hk_task
    );

    start_periodic_task(
        rtems_build_name('L', 'O', 'G', '1'),
        PL_LOG_PRIORITY,
        RTEMS_MINIMUM_STACK_SIZE,
        pl_log_task
    );

    sc = tmtc_start_tasks();

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf("ERROR starting TM/TC tasks: %s\n", rtems_status_text(sc));
        rtems_shutdown_executive(1);
    }

#if RUN_SECONDS > 0
    /*
     * Timed run for the timing analysis.
     * RUN_SECONDS / 10 ms per tick = RUN_SECONDS * 100 ticks.
     */
    rtems_task_wake_after(RUN_SECONDS * 100);

    printf("\n");
    printf("============================================\n");
    printf(" Rate Monotonic Statistics - Lab 4.3\n");
    printf("============================================\n");

    rtems_rate_monotonic_report_statistics();

    printf("\n");
    printf("Deadline misses: ACS_CTRL=%lu TM_HK=%lu PL_LOG=%lu\n",
           (unsigned long)acs_ctrl_misses,
           (unsigned long)tm_hk_misses,
           (unsigned long)pl_log_misses);
    printf("TM packets dropped (queue full): %lu\n",
           (unsigned long)tmtc_tm_dropped());
    printf("\nSimulation finished.\n");

    rtems_shutdown_executive(0);
#else
    rtems_task_exit();
#endif
}
