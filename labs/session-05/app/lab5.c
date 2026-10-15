#include <rtems.h>
#include <stdio.h>
#include <stdint.h>

#include "fdir.h"
#include "fdir_app.h"
#include "finj.h"
#include "services.h"
#include "tmtc5.h"

/*
 * ============================================================
 * Session 5 - FDIR application (Labs 5.1 to 5.4)
 * ============================================================
 *
 * Periodic, supervised (Lab 3.2 task set, priorities unchanged):
 *
 * ACS_CTRL : T = 100 ms,  ~20 ms work                    P = 10
 * TM_HK    : T = 500 ms,  sampling, monitoring, HK, ~50 ms P = 20
 * PL_LOG   : T = 1000 ms, ~100 ms work, only if payload on P = 30
 *
 * FDIR and support:
 *
 * SUP      : T = 2000 ms, liveness check, inhibit expiry,
 *            kicks the watchdog                            P = 5
 * SCHED    : T = 1000 ms, releases due scheduled commands  P = 45
 * TC_RX    : aperiodic, ground commands                    P = 40
 * TM_TX    : aperiodic, downlink                           P = 50
 * WDOG     : timer service routine every 100 ms (clock
 *            tick interrupt): the hardware watchdog model
 *
 * Build variants (see Makefile):
 *   test build    -DFDIR_TEST_BUILD: fault injection service 200
 *   flight build  no injection code at all
 *   timing build  flight build with RUN_SECONDS > 0: runs that long,
 *                 prints the rate monotonic statistics, shuts down
 *
 * 1 tick = 10 ms
 */

#ifndef RUN_SECONDS
#define RUN_SECONDS 0
#endif

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER

#define TASK_STACK (8u * 1024u)

#define CONFIGURE_MAXIMUM_TASKS            8
#define CONFIGURE_MAXIMUM_PERIODS          5
#define CONFIGURE_MAXIMUM_SEMAPHORES       6
#define CONFIGURE_MAXIMUM_TIMERS           1
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES   1
#define CONFIGURE_MESSAGE_BUFFER_MEMORY \
    CONFIGURE_MESSAGE_BUFFERS_FOR_QUEUE(TM_QUEUE_DEPTH, PUS_MAX_PACKET)

/* TC_RX, TM_TX, TM_HK, SUP, SCHED use 8 KiB stacks. */
#define CONFIGURE_EXTRA_TASK_STACKS (5u * TASK_STACK)

#define CONFIGURE_MICROSECONDS_PER_TICK 10000

#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INIT_TASK_STACK_SIZE (16 * 1024)

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT

#include <rtems/confdefs.h>

/* Lab 3.2 workloads (iteration counts calibrated on SIS, not ms). */
#define ACS_WORK  43000u
#define HK_WORK   108000u
#define LOG_WORK  217000u

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

/*
 * Period objects are created once and survive a task restart: after
 * rtems_task_restart() the task enters its entry point again, finds
 * its period already created, and simply waits for the next one.
 */
static rtems_id make_period(rtems_id *id, rtems_name name)
{
    if (*id == 0)
    {
        if (rtems_rate_monotonic_create(name, id) != RTEMS_SUCCESSFUL)
        {
            rtems_task_exit();
        }
    }
    return *id;
}

#ifdef FDIR_TEST_BUILD
static rtems_id hang_sem;

void finj_block_forever(void)
{
    for (;;)
    {
        (void)rtems_semaphore_obtain(hang_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
    }
}
#endif


/* ============================================================
 * Supervised periodic tasks
 * ============================================================
 */

static rtems_task acs_ctrl_task(rtems_task_argument arg)
{
    static rtems_id period;
    (void)arg;
    make_period(&period, rtems_build_name('A', 'C', 'S', '1'));

    while (1)
    {
        (void)rtems_rate_monotonic_period(period, ACS_PERIOD_TICKS);
        FINJ_TASK_POINT(TASK_ACS);
        fdir_heartbeat(TASK_ACS);
        do_bounded_work(ACS_WORK);
    }
}

static rtems_task tm_hk_task(rtems_task_argument arg)
{
    static rtems_id period;
    (void)arg;
    make_period(&period, rtems_build_name('H', 'K', '1', '1'));

    while (1)
    {
        (void)rtems_rate_monotonic_period(period, HK_PERIOD_TICKS);
        FINJ_TASK_POINT(TASK_HK);
        fdir_heartbeat(TASK_HK);
        fdir_hk_cycle();
        do_bounded_work(HK_WORK);
    }
}

static rtems_task pl_log_task(rtems_task_argument arg)
{
    static rtems_id period;
    (void)arg;
    make_period(&period, rtems_build_name('L', 'O', 'G', '1'));

    while (1)
    {
        (void)rtems_rate_monotonic_period(period, LOG_PERIOD_TICKS);
        FINJ_TASK_POINT(TASK_LOG);
        fdir_heartbeat(TASK_LOG);
        if (fdir_payload_on())          /* suspended in DEGRADED and SAFE */
        {
            do_bounded_work(LOG_WORK);
        }
    }
}


/* ============================================================
 * FDIR support tasks
 * ============================================================
 */

static rtems_task supervisor_task(rtems_task_argument arg)
{
    static rtems_id period;
    (void)arg;
    make_period(&period, rtems_build_name('S', 'U', 'P', '1'));

    /* The first call only starts the period: the first check happens
     * one full interval later, after every task has had time to beat. */
    (void)rtems_rate_monotonic_period(period, SUP_PERIOD_TICKS);

    while (1)
    {
        (void)rtems_rate_monotonic_period(period, SUP_PERIOD_TICKS);
        FINJ_SUPERVISOR_POINT();
        fdir_supervisor_cycle();
    }
}

static rtems_task sched_task(rtems_task_argument arg)
{
    static rtems_id period;
    (void)arg;
    make_period(&period, rtems_build_name('S', 'C', 'H', 'D'));

    while (1)
    {
        (void)rtems_rate_monotonic_period(period, SCHED_PERIOD_TICKS);
        fdir_sched_cycle();
    }
}


/* ============================================================
 * Init
 * ============================================================
 */

static rtems_id create_task(rtems_name name, rtems_task_priority prio,
                            size_t stack, rtems_attribute attr)
{
    rtems_id id;
    rtems_status_code sc = rtems_task_create(name, prio, stack,
                                             RTEMS_DEFAULT_MODES, attr, &id);
    if (sc != RTEMS_SUCCESSFUL)
    {
        printf("ERROR creating task: %s\n", rtems_status_text(sc));
        rtems_shutdown_executive(1);
    }
    return id;
}

static void start(rtems_id id, rtems_task_entry entry)
{
    rtems_status_code sc = rtems_task_start(id, entry, 0);
    if (sc != RTEMS_SUCCESSFUL)
    {
        printf("ERROR starting task: %s\n", rtems_status_text(sc));
        rtems_shutdown_executive(1);
    }
}

rtems_task Init(rtems_task_argument arg)
{
    rtems_status_code sc;
    rtems_id acs, hk, log, sup, sched;

    (void)arg;

    printf("\n");
    printf("============================================\n");
    printf(" Session 5 - FDIR, %s\n",
#ifdef FDIR_TEST_BUILD
           "TEST build (fault injection enabled)"
#else
           "FLIGHT build"
#endif
    );
    printf("============================================\n");
    printf("Supervisor: check %u ms, %u restarts per %u s, watchdog %u ms\n",
           (unsigned)SUP_CHECK_MS, (unsigned)SUP_MAX_RESTARTS_N,
           (unsigned)(SUP_WINDOW_MS / 1000u), (unsigned)WDG_TIMEOUT_MS);
    printf("\n");
    fflush(stdout);

    sc = tmtc_init();
    if (sc == RTEMS_SUCCESSFUL)
    {
        sc = fdir_boot();
    }
    if (sc != RTEMS_SUCCESSFUL || services_register(tmtc_tc_ctx()) != 0)
    {
        printf("ERROR during FDIR start up: %s\n", rtems_status_text(sc));
        rtems_shutdown_executive(1);
    }
#ifdef FDIR_TEST_BUILD
    (void)rtems_semaphore_create(rtems_build_name('H', 'A', 'N', 'G'), 0,
                                 RTEMS_SIMPLE_BINARY_SEMAPHORE, 0, &hang_sem);
#endif

    sc = tmtc_start_tasks();
    if (sc != RTEMS_SUCCESSFUL)
    {
        printf("ERROR starting TM/TC tasks: %s\n", rtems_status_text(sc));
        rtems_shutdown_executive(1);
    }

    acs   = create_task(rtems_build_name('A', 'C', 'S', '1'), ACS_PRIORITY,
                        RTEMS_MINIMUM_STACK_SIZE, RTEMS_DEFAULT_ATTRIBUTES);
    hk    = create_task(rtems_build_name('H', 'K', '1', '1'), HK_PRIORITY,
                        TASK_STACK, RTEMS_DEFAULT_ATTRIBUTES);
    log   = create_task(rtems_build_name('L', 'O', 'G', '1'), LOG_PRIORITY,
                        RTEMS_MINIMUM_STACK_SIZE, RTEMS_DEFAULT_ATTRIBUTES);
    sup   = create_task(rtems_build_name('S', 'U', 'P', '1'), SUP_PRIORITY,
                        TASK_STACK, RTEMS_DEFAULT_ATTRIBUTES);
    sched = create_task(rtems_build_name('S', 'C', 'H', 'D'), SCHED_PRIORITY,
                        TASK_STACK, RTEMS_DEFAULT_ATTRIBUTES);

    /* Escalation targets: losing attitude control or housekeeping is
     * critical (SAFE); losing payload logging is not (DEGRADED). */
    if (fdir_supervise("ACS_CTRL", 100, acs, MODE_SAFE) != (int)TASK_ACS ||
        fdir_supervise("TM_HK", 500, hk, MODE_SAFE) != (int)TASK_HK ||
        fdir_supervise("PL_LOG", 1000, log, MODE_DEGRADED) != (int)TASK_LOG)
    {
        printf("ERROR registering supervised tasks\n");
        rtems_shutdown_executive(1);
    }

    start(acs, acs_ctrl_task);
    start(hk, tm_hk_task);
    start(log, pl_log_task);
    start(sched, sched_task);
    start(sup, supervisor_task);
    fdir_watchdog_start();

#if RUN_SECONDS > 0
    rtems_task_wake_after(RUN_SECONDS * 100);
    printf("\n============================================\n");
    printf(" Rate Monotonic Statistics - Session 5\n");
    printf("============================================\n");
    rtems_rate_monotonic_report_statistics();
    printf("\nSimulation finished.\n");
    rtems_shutdown_executive(0);
#else
    rtems_task_exit();
#endif
}
