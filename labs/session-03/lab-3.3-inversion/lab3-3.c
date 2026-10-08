#include <rtems.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

/*
 * ============================================================
 * Lab 3.3 - Priority Inversion (one-shot, deterministic)
 *
 * Stage 1: USE_PRIORITY_INHERIT = 0  -> priority inversion
 * Stage 2: USE_PRIORITY_INHERIT = 1  -> priority inheritance
 *
 * Run Stage 1 and Stage 2 with MED_MULTIPLIER = 1, 2, 4.
 *
 * Environment: state clearly in the report that this runs on SIS
 * (not QEMU).
 * ============================================================
 */

#define USE_PRIORITY_INHERIT 1

/* MED workload = MED_BASE_ITERATIONS * MED_MULTIPLIER (x1, x2, x4) */
#define MED_MULTIPLIER 1

/*
 * These are ITERATION COUNTS, not milliseconds. Calibrate them on SIS
 * so that cpu_work() runs for a few tens of milliseconds.
 */
#define LOW_WORK_ITERATIONS 200000u
#define MED_BASE_ITERATIONS 100000u

/* RTEMS priorities: a SMALLER number means a HIGHER priority */
#define PRIO_HIGH 10
#define PRIO_MED 50
#define PRIO_LOW 100

#define TASK_STACK_SIZE (8 * 1024)

/*
 * ------------------------------------------------------------
 * RTEMS configuration
 * ------------------------------------------------------------
 */
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_TASKS 5
#define CONFIGURE_MAXIMUM_SEMAPHORES 8

/*
 * LOW/MED/HIGH are created with TASK_STACK_SIZE, which is larger than the
 * default stack size confdefs.h budgets per task. Reserve the extra space
 * in the RTEMS workspace, otherwise rtems_task_create() fails (RTEMS_UNSATISFIED).
 */
#define CONFIGURE_EXTRA_TASK_STACKS (3 * TASK_STACK_SIZE)

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_STACK_SIZE (16 * 1024)

/*
 * Init calls printf(). On LEON3 (SPARC with FPU) newlib's printf may execute
 * FPU instructions, and RTEMS only allows that in tasks created with the
 * RTEMS_FLOATING_POINT attribute. Without it: fatal error 38
 * (INTERNAL_ERROR_ILLEGAL_USE_OF_FLOATING_POINT_UNIT).
 */
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INIT

#include <rtems/confdefs.h>

/*
 * ------------------------------------------------------------
 * Global objects
 * ------------------------------------------------------------
 */
static rtems_id mutex_id;           /* shared resource (owner-tracked)   */
static rtems_id low_has_mutex_id;   /* signal LOW  -> HIGH               */
static rtems_id high_attempting_id; /* signal HIGH -> MED                */
static rtems_id done_id;            /* counting: 3 tasks report done    */

static rtems_id low_task_id;
static rtems_id med_task_id;
static rtems_id high_task_id;

/* Timestamps (only stored here, printed after the experiment ends) */
static volatile uint64_t low_acquire_ns;
static volatile uint64_t low_release_ns;
static volatile uint64_t high_request_ns;
static volatile uint64_t high_acquire_ns;
static volatile uint64_t med_start_ns;
static volatile uint64_t med_end_ns;

/*
 * ------------------------------------------------------------
 * CPU-bound workload (no sleeping)
 * ------------------------------------------------------------
 */
static void cpu_work(uint32_t iterations)
{
    volatile uint32_t x = 0;

    for (uint32_t i = 0; i < iterations; ++i)
    {
        x += i;
        x ^= (x << 1);
        x ^= (x >> 3);
    }
}

static void fatal_if_failed(rtems_status_code sc, uint32_t code)
{
    if (sc != RTEMS_SUCCESSFUL)
    {
        rtems_fatal(RTEMS_FATAL_SOURCE_APPLICATION, code);
    }
}

/*
 * ------------------------------------------------------------
 * T_LOW (priority 100)
 *
 * 1. Acquire real mutex
 * 2. Signal LOW_HAS_MUTEX
 * 3. Critical section (CPU work)
 * 4. Release mutex
 *
 * NO printf inside the critical section.
 * ------------------------------------------------------------
 */
static rtems_task low_task(rtems_task_argument arg)
{
    (void)arg;

    fatal_if_failed(
        rtems_semaphore_obtain(mutex_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT), 20);

    low_acquire_ns = rtems_clock_get_uptime_nanoseconds();

    /* Tell HIGH that LOW owns the mutex. HIGH preempts LOW immediately. */
    fatal_if_failed(rtems_semaphore_release(low_has_mutex_id), 21);

    /* Critical section: the part that remains after HIGH/MED preemption */
    cpu_work(LOW_WORK_ITERATIONS);

    low_release_ns = rtems_clock_get_uptime_nanoseconds();

    fatal_if_failed(rtems_semaphore_release(mutex_id), 22);

    fatal_if_failed(rtems_semaphore_release(done_id), 23);
    rtems_task_exit();
}

/*
 * ------------------------------------------------------------
 * T_HIGH (priority 10)
 *
 * 1. Wait LOW_HAS_MUTEX
 * 2. Signal HIGH_ATTEMPTING (MED se runnable)
 * 3. Record request time, obtain mutex (blocks because LOW owns it)
 * 4. Record acquire time, release
 * ------------------------------------------------------------
 */
static rtems_task high_task(rtems_task_argument arg)
{
    (void)arg;

    fatal_if_failed(
        rtems_semaphore_obtain(low_has_mutex_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT),
        30);

    /*
     * MED has lower priority than HIGH, so it is only "runnable"
     * and does not run until HIGH blocks on the mutex.
     */
    fatal_if_failed(rtems_semaphore_release(high_attempting_id), 31);

    high_request_ns = rtems_clock_get_uptime_nanoseconds();

    fatal_if_failed(
        rtems_semaphore_obtain(mutex_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT), 32);

    high_acquire_ns = rtems_clock_get_uptime_nanoseconds();

    fatal_if_failed(rtems_semaphore_release(mutex_id), 33);

    fatal_if_failed(rtems_semaphore_release(done_id), 34);
    rtems_task_exit();
}

/*
 * ------------------------------------------------------------
 * T_MED (priority 50)
 *
 * 1. Wait HIGH_ATTEMPTING
 * 2. CPU-intensive work (does not use the mutex)
 * ------------------------------------------------------------
 */
static rtems_task med_task(rtems_task_argument arg)
{
    (void)arg;

    fatal_if_failed(
        rtems_semaphore_obtain(high_attempting_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT),
        40);

    med_start_ns = rtems_clock_get_uptime_nanoseconds();

    cpu_work(MED_BASE_ITERATIONS * MED_MULTIPLIER *2);

    med_end_ns = rtems_clock_get_uptime_nanoseconds();

    fatal_if_failed(rtems_semaphore_release(done_id), 41);
    rtems_task_exit();
}

/*
 * ------------------------------------------------------------
 * Create objects
 * ------------------------------------------------------------
 */
static void create_semaphores(void)
{
    rtems_attribute attrs = RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY;

#if USE_PRIORITY_INHERIT
    attrs |= RTEMS_INHERIT_PRIORITY;
#endif

    /* Shared resource: mutex with owner tracking */
    fatal_if_failed(
        rtems_semaphore_create(
            rtems_build_name('M', 'T', 'X', '1'), 1, attrs, 0, &mutex_id),
        1);

    /* Signalling: simple binary semaphore, no owner, no inheritance */
    fatal_if_failed(
        rtems_semaphore_create(
            rtems_build_name('S', 'L', 'O', 'W'), 0,
            RTEMS_SIMPLE_BINARY_SEMAPHORE | RTEMS_PRIORITY, 0,
            &low_has_mutex_id),
        2);

    fatal_if_failed(
        rtems_semaphore_create(
            rtems_build_name('S', 'H', 'I', 'G'), 0,
            RTEMS_SIMPLE_BINARY_SEMAPHORE | RTEMS_PRIORITY, 0,
            &high_attempting_id),
        3);

    /* Counting semaphore: Init waits for the 3 tasks to finish */
    fatal_if_failed(
        rtems_semaphore_create(
            rtems_build_name('D', 'O', 'N', 'E'), 0,
            RTEMS_COUNTING_SEMAPHORE | RTEMS_PRIORITY, 0, &done_id),
        4);
}

static void create_and_start_tasks(void)
{
    fatal_if_failed(
        rtems_task_create(rtems_build_name('L', 'O', 'W', ' '), PRIO_LOW,
                          TASK_STACK_SIZE, RTEMS_DEFAULT_MODES,
                          RTEMS_DEFAULT_ATTRIBUTES, &low_task_id),
        10);

    fatal_if_failed(
        rtems_task_create(rtems_build_name('M', 'E', 'D', ' '), PRIO_MED,
                          TASK_STACK_SIZE, RTEMS_DEFAULT_MODES,
                          RTEMS_DEFAULT_ATTRIBUTES, &med_task_id),
        11);

    fatal_if_failed(
        rtems_task_create(rtems_build_name('H', 'I', 'G', 'H'), PRIO_HIGH,
                          TASK_STACK_SIZE, RTEMS_DEFAULT_MODES,
                          RTEMS_DEFAULT_ATTRIBUTES, &high_task_id),
        12);

    /*
     * Init (priority 1) is higher than all 3 tasks, so none of them
     * runs after being started. When Init blocks on done_id, the order is:
     *   HIGH runs -> blocks waiting for LOW_HAS_MUTEX
     *   MED  runs -> blocks waiting for HIGH_ATTEMPTING
     *   LOW  runs -> obtains the mutex ...
     * This order is fixed and does not depend on any delay.
     * (Assumes 1 core; on SMP, restrict the affinity.)
     */
    fatal_if_failed(rtems_task_start(low_task_id, low_task, 0), 13);
    fatal_if_failed(rtems_task_start(med_task_id, med_task, 0), 14);
    fatal_if_failed(rtems_task_start(high_task_id, high_task, 0), 15);
}

/*
 * ------------------------------------------------------------
 * Print results (after the experiment has finished)
 * ------------------------------------------------------------
 */
static void print_results(void)
{
    uint64_t t0 = low_acquire_ns;

    uint64_t low_cs_ns = low_release_ns - low_acquire_ns;
    uint64_t high_block_ns = high_acquire_ns - high_request_ns;
    uint64_t med_work_ns = med_end_ns - med_start_ns;

    printf("\n============================================\n");
#if USE_PRIORITY_INHERIT
    printf(" Stage 2: PRIORITY INHERITANCE, MED x%d\n", MED_MULTIPLIER);
#else
    printf(" Stage 1: NO PRIORITY INHERITANCE, MED x%d\n", MED_MULTIPLIER);
#endif
    printf("============================================\n");

    printf("Timestamps (us, relative to LOW acquire):\n");
    printf("  LOW  acquire  : %" PRIu64 "\n", (low_acquire_ns - t0) / 1000);
    printf("  HIGH request  : %" PRIu64 "\n", (high_request_ns - t0) / 1000);
    printf("  MED  start    : %" PRIu64 "\n", (med_start_ns - t0) / 1000);
    printf("  MED  end      : %" PRIu64 "\n", (med_end_ns - t0) / 1000);
    printf("  LOW  release  : %" PRIu64 "\n", (low_release_ns - t0) / 1000);
    printf("  HIGH acquire  : %" PRIu64 "\n", (high_acquire_ns - t0) / 1000);

    printf("Durations (us):\n");
    printf("  LOW  critical section (acquire->release) : %" PRIu64 "\n",
           low_cs_ns / 1000);
    printf("  MED  work                                : %" PRIu64 "\n",
           med_work_ns / 1000);
    printf("  HIGH blocking time (acquire - request)   : %" PRIu64 "\n",
           high_block_ns / 1000);

    printf("CSV,%d,%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
           USE_PRIORITY_INHERIT, MED_MULTIPLIER,
           low_cs_ns / 1000, med_work_ns / 1000, high_block_ns / 1000);
}

/*
 * ------------------------------------------------------------
 * Init
 * ------------------------------------------------------------
 */
rtems_task Init(rtems_task_argument arg)
{
    (void)arg;

    create_semaphores();
    create_and_start_tasks();

    /* Wait for LOW, MED, HIGH to report done (no long sleep) */
    for (int i = 0; i < 3; ++i)
    {
        fatal_if_failed(
            rtems_semaphore_obtain(done_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT), 50);
    }

    print_results();

    printf("\nSimulation finished.\n");
    rtems_shutdown_executive(0);
}