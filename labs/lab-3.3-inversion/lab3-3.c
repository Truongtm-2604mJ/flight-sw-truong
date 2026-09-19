#include <rtems.h>
#include <stdio.h>
#include <stdint.h>

/*
 * ============================================================
 * Lab 3.3 - Priority Inversion
 *
 * Stage 1:
 *   USE_PRIORITY_INHERIT = 0
 *   -> Priority inversion
 *
 * Stage 2:
 *   USE_PRIORITY_INHERIT = 1
 *   -> Priority inheritance
 * ============================================================
 */

#define USE_PRIORITY_INHERIT 0

/*
 * CPU workload.
 *
 * IMPORTANT:
 * These are iteration counts, not milliseconds.
 * Calibrate them experimentally on SIS.
 *
 *
 */

#define LOW_PERIOD_TICKS RTEMS_MILLISECONDS_TO_TICKS(500000)
#define MED_PERIOD_TICKS RTEMS_MILLISECONDS_TO_TICKS(200000)
#define HIGH_PERIOD_TICKS RTEMS_MILLISECONDS_TO_TICKS(100000)

#define LOW_WORK_ITERATIONS 200000
#define MED_WORK_ITERATIONS 100000

/*
 * ------------------------------------------------------------
 * RTEMS configuration
 * ------------------------------------------------------------
 */

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_TASKS 4
#define CONFIGURE_MAXIMUM_SEMAPHORES 4
#define CONFIGURE_MAXIMUM_PERIODS 3

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT

#define CONFIGURE_INIT_TASK_NAME rtems_build_name('I', 'N', 'I', 'T')

#include <rtems/confdefs.h>

/*
 * ------------------------------------------------------------
 * Global objects
 * ------------------------------------------------------------
 */

rtems_id mutex_id;

rtems_id low_has_mutex_id;
rtems_id high_attempting_id;

rtems_id low_task_id;
rtems_id med_task_id;
rtems_id high_task_id;

/*
 * ------------------------------------------------------------
 * Timing information
 * ------------------------------------------------------------
 */

uint64_t low_acquire_ns;
uint64_t low_release_ns;

uint64_t high_request_ns;
uint64_t high_acquire_ns;

uint64_t med_start_ns;
uint64_t med_end_ns;

/*
 * ------------------------------------------------------------
 * CPU-bound workload
 * ------------------------------------------------------------
 *
 * This represents actual CPU execution.
 *
 * Do NOT replace this with rtems_task_wake_after()
 * if the purpose is to demonstrate CPU-driven
 * priority inversion.
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

/*
 * ------------------------------------------------------------
 * T_LOW
 *
 * Priority = 100
 *
 * 1. Acquire mutex
 * 2. Notify T_HIGH that mutex is owned
 * 3. Execute long critical section
 * 4. Release mutex
 * ------------------------------------------------------------
 */

static rtems_task low_task(rtems_task_argument arg)
{
    rtems_id period_id;
    rtems_status_code sc;

    (void)arg;

    sc = rtems_rate_monotonic_create(
        rtems_build_name('L', 'O', 'W', '1'),
        &period_id);

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf(
            "LOW: rate monotonic create failed");

        rtems_task_exit();
    }

    printf("LOW started\n");

    while (1)
    {
        /*
         * Acquire shared mutex.
         */
        sc = rtems_semaphore_obtain(
            mutex_id,
            RTEMS_WAIT,
            RTEMS_NO_TIMEOUT);

        if (sc != RTEMS_SUCCESSFUL)
        {
            printf(
                "LOW: mutex obtain failed\n");

            rtems_task_exit();
        }

        low_acquire_ns =
            rtems_clock_get_uptime_nanoseconds();

        printf("LOW: mutex acquired\n");

        /*
         * Critical section.
         */
        cpu_work(LOW_WORK_ITERATIONS);

        low_release_ns =
            rtems_clock_get_uptime_nanoseconds();

        /*
         * Release mutex.
         */
        sc = rtems_semaphore_release(mutex_id);

        if (sc != RTEMS_SUCCESSFUL)
        {
            printf(
                "LOW: mutex release failed\n");
        }

        printf("LOW: mutex released\n");

        /*
         * Wait for next period.
         */
        sc = rtems_rate_monotonic_period(
            period_id,
            LOW_PERIOD_TICKS);

        if (sc == RTEMS_TIMEOUT)
        {
            printf("DEADLINE MISS: LOW\n");
        }
        else if (sc != RTEMS_SUCCESSFUL)
        {
            printf(
                "LOW period error\n");
        }
    }
}

/*
 * ------------------------------------------------------------
 * T_HIGH
 *
 * Priority = 10
 *
 * 1. Wait until T_LOW owns mutex
 * 2. Record request time
 * ------------------------------------------------------------
 */

static rtems_task high_task(rtems_task_argument arg)
{
    rtems_id period_id;
    rtems_status_code sc;

    (void)arg;

    sc = rtems_rate_monotonic_create(
        rtems_build_name('H', 'I', 'G', '1'),
        &period_id);

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf(
            "HIGH: rate monotonic create failed\n");

        rtems_task_exit();
    }

    printf("HIGH started\n");

    while (1)
    {
        /*
         * Start measuring mutex blocking time.
         */
        high_request_ns =
            rtems_clock_get_uptime_nanoseconds();

        /*
         * LOW owns the mutex, so HIGH blocks here.
         */
        sc = rtems_semaphore_obtain(
            mutex_id,
            RTEMS_WAIT,
            RTEMS_NO_TIMEOUT);

        if (sc != RTEMS_SUCCESSFUL)
        {
            printf(
                "HIGH: mutex obtain failed\n");

            rtems_task_exit();
        }

        high_acquire_ns =
            rtems_clock_get_uptime_nanoseconds();

        uint64_t blocked_ns =
            high_acquire_ns - high_request_ns;

        printf("HIGH: mutex acquired\n");

        /*
         * Release mutex.
         */
        sc = rtems_semaphore_release(mutex_id);

        if (sc != RTEMS_SUCCESSFUL)
        {
            printf(
                "HIGH: mutex release failed\n");
        }

        /*
         * Wait for next period.
         */
        sc = rtems_rate_monotonic_period(
            period_id,
            HIGH_PERIOD_TICKS);

        if (sc == RTEMS_TIMEOUT)
        {
            printf("DEADLINE MISS: HIGH\n");
        }
        else if (sc != RTEMS_SUCCESSFUL)
        {
            printf(
                "HIGH period error\n");
        }
    }
}

static rtems_task med_task(rtems_task_argument arg)
{
    rtems_id period_id;
    rtems_status_code sc;

    (void)arg;

    sc = rtems_rate_monotonic_create(
        rtems_build_name('M', 'E', 'D', '1'),
        &period_id);

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf(
            "MED: rate monotonic create failed\n");

        rtems_task_exit();
    }

    printf("MED started\n");

    while (1)
    {
        med_start_ns =
            rtems_clock_get_uptime_nanoseconds();

        printf("MED: CPU workload started\n");

        /*
         * MED does not use the mutex.
         */
        cpu_work(MED_WORK_ITERATIONS);

        med_end_ns =
            rtems_clock_get_uptime_nanoseconds();

        printf("MED: CPU workload finished\n");

        /*
         * Wait for next period.
         */
        sc = rtems_rate_monotonic_period(
            period_id,
            MED_PERIOD_TICKS);

        if (sc == RTEMS_TIMEOUT)
        {
            printf("DEADLINE MISS: MED\n");
        }
        else if (sc != RTEMS_SUCCESSFUL)
        {
            printf(
                "MED period error\n");
        }
    }
}

/*
 * ------------------------------------------------------------
 * Create semaphore
 * ------------------------------------------------------------
 */

static void create_mutex(void)
{
    rtems_attribute attributes =
        RTEMS_BINARY_SEMAPHORE |
        RTEMS_PRIORITY;

#if USE_PRIORITY_INHERIT
    attributes |= RTEMS_INHERIT_PRIORITY;
#endif

    rtems_status_code sc =
        rtems_semaphore_create(
            rtems_build_name('M', 'T', 'X', '1'),
            1,
            attributes,
            0,
            &mutex_id);

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf("[INIT] ERROR: cannot create mutex\n");
        rtems_fatal(RTEMS_FATAL_SOURCE_APPLICATION, 1);
    }
}

// /*
//  * ------------------------------------------------------------
//  * Create synchronization semaphores
//  * ------------------------------------------------------------
//  */

// static void create_sync_semaphores(void)
// {
//     rtems_status_code sc;

//     /*
//      * T_LOW -> T_HIGH
//      *
//      * Count = 0 initially.
//      */
//     sc = rtems_semaphore_create(
//         rtems_build_name('L', 'O', 'W', '1'),
//         0,
//         RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY,
//         0,
//         &low_has_mutex_id);

//     if (sc != RTEMS_SUCCESSFUL)
//     {
//         printf("[INIT] ERROR: LOW sync semaphore\n");
//         rtems_fatal(RTEMS_FATAL_SOURCE_APPLICATION, 2);
//     }

//     /*
//      * T_HIGH -> T_MED
//      *
//      * Count = 0 initially.
//      */
//     sc = rtems_semaphore_create(
//         rtems_build_name('H', 'I', 'G', '1'),
//         0,
//         RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY,
//         0,
//         &high_attempting_id);

//     if (sc != RTEMS_SUCCESSFUL)
//     {
//         printf("[INIT] ERROR: HIGH sync semaphore\n");
//         rtems_fatal(RTEMS_FATAL_SOURCE_APPLICATION, 3);
//     }
// }

/*
 * ------------------------------------------------------------
 * Create tasks
 * ------------------------------------------------------------
 */

static void create_tasks(void)
{
    rtems_status_code sc;

    /*
     * --------------------------------------------------------
     * T_LOW
     * Priority 100
     * --------------------------------------------------------
     */

    sc = rtems_task_create(
        rtems_build_name('L', 'O', 'W', ' '),
        100,
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &low_task_id);

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf("[INIT] ERROR: cannot create LOW\n");
        rtems_fatal(RTEMS_FATAL_SOURCE_APPLICATION, 10);
    }

    /*
     * --------------------------------------------------------
     * T_MED
     * Priority 50
     * --------------------------------------------------------
     */

    sc = rtems_task_create(
        rtems_build_name('M', 'E', 'D', ' '),
        50,
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &med_task_id);

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf(
            "[INIT] ERROR: cannot create MED\n");
        rtems_fatal(RTEMS_FATAL_SOURCE_APPLICATION, 11);
    }

    /*
     * --------------------------------------------------------
     * T_HIGH
     * Priority 10
     * --------------------------------------------------------
     */

    sc = rtems_task_create(
        rtems_build_name('H', 'I', 'G', 'H'),
        10,
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &high_task_id);

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf("[INIT] ERROR: cannot create HIGH\n");
        rtems_fatal(RTEMS_FATAL_SOURCE_APPLICATION, 12);
    }

    /*
     * Start LOW first.
     *
     * LOW must acquire the mutex before HIGH
     * starts.
     */
    sc = rtems_task_start(
        low_task_id,
        low_task,
        0);

    if (sc != RTEMS_SUCCESSFUL)
    {
        rtems_fatal(RTEMS_FATAL_SOURCE_APPLICATION, 13);
    }

    rtems_task_wake_after(1000);

    /*
     * Start MED.
     *
     * MED immediately blocks on HIGH_ATTEMPTING.
     */
    sc = rtems_task_start(
        med_task_id,
        med_task,
        0);

    if (sc != RTEMS_SUCCESSFUL)
    {
        rtems_fatal(RTEMS_FATAL_SOURCE_APPLICATION, 15);
    }

    /*
     * Start HIGH.
     *
     * HIGH waits for LOW_HAS_MUTEX.
     */
    sc = rtems_task_start(
        high_task_id,
        high_task,
        0);

    if (sc != RTEMS_SUCCESSFUL)
    {
        rtems_fatal(RTEMS_FATAL_SOURCE_APPLICATION, 14);
    }
}

/*
 * ------------------------------------------------------------
 * Print results
 * ------------------------------------------------------------
 */

static void print_results(void)
{
    uint64_t low_cs_ns =
        low_release_ns - low_acquire_ns;

    uint64_t high_block_ns =
        high_acquire_ns - high_request_ns;

    uint64_t med_work_ns =
        med_end_ns - med_start_ns;

    printf("\n");
    printf("============================================\n");

#if USE_PRIORITY_INHERIT
    printf(" Stage 2: PRIORITY INHERITANCE\n");
#else
    printf(" Stage 1: NO PRIORITY INHERITANCE\n");
#endif

    printf("============================================\n");
}

/*
 * ------------------------------------------------------------
 * Init
 * ------------------------------------------------------------
 */

rtems_task Init(rtems_task_argument arg)
{
    (void)arg;

#if USE_PRIORITY_INHERIT
    printf("\n");
    printf("============================================\n");
    printf(" Lab 3.3 - Stage 2\n");
    printf(" Priority Inheritance ENABLED\n");
    printf("============================================\n");
#else
    printf("\n");
    printf("============================================\n");
    printf(" Lab 3.3 - Stage 1\n");
    printf(" Priority Inheritance DISABLED\n");
    printf("============================================\n");
#endif

    /*
     * Create shared mutex.
     */
    create_mutex();

    /*
     * Create synchronization semaphores.
     */
    // create_sync_semaphores();

    /*
     * Create and start LOW, HIGH, MED.
     */
    create_tasks();

    /*
     * Allow experiment to finish.
     *
     * 1000 ticks = 10 seconds if
     * CONFIGURE_MICROSECONDS_PER_TICK = 10000.
     */
    rtems_task_wake_after(100000);

    /*
     * Print measured results.
     */
    print_results();

    printf("\nSimulation finished.\n");

    rtems_shutdown_executive(0);
}