/* HOST SIMULATION ONLY - see README.md in this directory. */
#define _POSIX_C_SOURCE 200809L

#include "rtems.h"

#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TICK_NS        10000000ull   /* CONFIGURE_MICROSECONDS_PER_TICK */
#define SLICE_NS       2000000ull    /* real time polling slice          */
#define MAX_TASKS      16
#define MAX_PERIODS    8
#define MAX_QUEUES     4
#define MAX_SEMS       8
#define MAX_TIMERS     4

/* ---- Time ---------------------------------------------------------------- */

static uint64_t boot_real_ns;
static uint64_t speedup = 1;

static uint64_t real_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint64_t rtems_clock_get_uptime_nanoseconds(void)
{
    return (real_ns() - boot_real_ns) * speedup;
}

static void real_sleep(uint64_t ns)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(ns / 1000000000ull);
    ts.tv_nsec = (long)(ns % 1000000000ull);
    while (nanosleep(&ts, &ts) != 0) {
    }
}

/* ---- Tasks and restart ----------------------------------------------------- */

typedef struct {
    rtems_task_entry    entry;
    rtems_task_argument arg;
    pthread_t           thread;
    sigjmp_buf          env;
    volatile int        restart_pending;
    rtems_task_argument restart_arg;
} sim_task_t;

static sim_task_t tasks[MAX_TASKS];
static uint32_t task_count;
static __thread sim_task_t *current;
static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;

/* Called at every blocking point: a pending restart unwinds the task's
 * stack back to its entry. */
static void restart_point(void)
{
    if (current != NULL && current->restart_pending) {
        siglongjmp(current->env, 1);
    }
}

/* Sleep until a simulated time, in slices, honouring restarts. */
static void sleep_until_sim(uint64_t sim_t)
{
    for (;;) {
        uint64_t now = rtems_clock_get_uptime_nanoseconds();
        uint64_t left_real;
        restart_point();
        if (now >= sim_t) {
            return;
        }
        left_real = (sim_t - now) / speedup;
        real_sleep(left_real < SLICE_NS ? (left_real ? left_real : 1)
                                        : SLICE_NS);
    }
}

static void *task_trampoline(void *p)
{
    sim_task_t *t = p;
    current = t;
    if (sigsetjmp(t->env, 0) != 0) {
        t->arg = t->restart_arg;
    }
    t->restart_pending = 0;
    t->entry(t->arg);
    return NULL;
}

const char *rtems_status_text(rtems_status_code sc)
{
    switch (sc) {
    case RTEMS_SUCCESSFUL:   return "RTEMS_SUCCESSFUL";
    case RTEMS_INVALID_ID:   return "RTEMS_INVALID_ID";
    case RTEMS_TOO_MANY:     return "RTEMS_TOO_MANY";
    case RTEMS_TIMEOUT:      return "RTEMS_TIMEOUT";
    case RTEMS_INVALID_SIZE: return "RTEMS_INVALID_SIZE";
    case RTEMS_UNSATISFIED:  return "RTEMS_UNSATISFIED";
    default:                 return "?";
    }
}

rtems_status_code rtems_task_create(rtems_name name,
                                    rtems_task_priority priority,
                                    size_t stack_size, rtems_mode modes,
                                    rtems_attribute attributes, rtems_id *id)
{
    (void)name; (void)priority; (void)stack_size; (void)modes; (void)attributes;
    pthread_mutex_lock(&table_lock);
    if (task_count >= MAX_TASKS) {
        pthread_mutex_unlock(&table_lock);
        return RTEMS_TOO_MANY;
    }
    *id = task_count++ + 1u;     /* 0 is never a valid id */
    pthread_mutex_unlock(&table_lock);
    return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_task_start(rtems_id id, rtems_task_entry entry,
                                   rtems_task_argument arg)
{
    sim_task_t *t;
    if (id == 0u || id > task_count) {
        return RTEMS_INVALID_ID;
    }
    t = &tasks[id - 1u];
    t->entry = entry;
    t->arg   = arg;
    if (pthread_create(&t->thread, NULL, task_trampoline, t) != 0) {
        return RTEMS_UNSATISFIED;
    }
    return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_task_restart(rtems_id id, rtems_task_argument arg)
{
    if (id == 0u || id > task_count) {
        return RTEMS_INVALID_ID;
    }
    tasks[id - 1u].restart_arg = arg;
    tasks[id - 1u].restart_pending = 1;
    return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_task_wake_after(rtems_interval ticks)
{
    sleep_until_sim(rtems_clock_get_uptime_nanoseconds() +
                    (uint64_t)ticks * TICK_NS);
    return RTEMS_SUCCESSFUL;
}

void rtems_task_exit(void)
{
    pthread_exit(NULL);
}

void rtems_shutdown_executive(uint32_t result)
{
    fflush(stdout);
    _exit((int)result);
}

void rtems_fatal(rtems_fatal_source source, rtems_fatal_code code)
{
    char line[96];
    int n = snprintf(line, sizeof(line),
                     "[ HOST-SIM fatal: source %u code 0x%08X: processor reset ]\n",
                     (unsigned)source, (unsigned)code);
    fflush(stdout);
    if (n > 0 && write(STDOUT_FILENO, line, (size_t)n) < 0) {
        n = 0;   /* nothing more can be done on the way down */
    }
    _exit(3);
}

/* ---- Interrupt disable (shared with the timer thread) ------------------- */

static pthread_mutex_t isr_lock;

void hostsim_interrupt_disable(void) { pthread_mutex_lock(&isr_lock); }
void hostsim_interrupt_enable(void)  { pthread_mutex_unlock(&isr_lock); }

/* ---- Rate monotonic periods ------------------------------------------------ */

typedef struct {
    rtems_name name;
    int        started;
    uint64_t   deadline_ns;
    uint32_t   count;
    uint32_t   missed;
} sim_period_t;

static sim_period_t periods[MAX_PERIODS];
static uint32_t period_count;

rtems_status_code rtems_rate_monotonic_create(rtems_name name, rtems_id *id)
{
    pthread_mutex_lock(&table_lock);
    if (period_count >= MAX_PERIODS) {
        pthread_mutex_unlock(&table_lock);
        return RTEMS_TOO_MANY;
    }
    periods[period_count].name = name;
    *id = period_count++ + 1u;
    pthread_mutex_unlock(&table_lock);
    return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_rate_monotonic_period(rtems_id id,
                                              rtems_interval length)
{
    sim_period_t *p;
    uint64_t now = rtems_clock_get_uptime_nanoseconds();
    uint64_t len = (uint64_t)length * TICK_NS;

    if (id == 0u || id > period_count) {
        return RTEMS_INVALID_ID;
    }
    p = &periods[id - 1u];

    restart_point();
    if (!p->started) {
        p->started = 1;
        p->deadline_ns = now + len;
        return RTEMS_SUCCESSFUL;
    }
    p->count++;
    if (now > p->deadline_ns) {
        p->missed++;
        p->deadline_ns = now + len;
        return RTEMS_TIMEOUT;
    }
    sleep_until_sim(p->deadline_ns);
    p->deadline_ns += len;
    return RTEMS_SUCCESSFUL;
}

void rtems_rate_monotonic_report_statistics(void)
{
    uint32_t i;
    printf("HOST SIMULATION - period counts only, no timing data\n");
    for (i = 0; i < period_count; i++) {
        rtems_name n = periods[i].name;
        printf("  %c%c%c%c  %5u  %6u\n",
               (char)(n >> 24), (char)(n >> 16), (char)(n >> 8), (char)n,
               (unsigned)periods[i].count, (unsigned)periods[i].missed);
    }
}

/* ---- Message queues ---------------------------------------------------------- */

typedef struct {
    uint8_t        *storage;
    size_t         *sizes;
    uint32_t        capacity;
    size_t          max_size;
    uint32_t        head;
    uint32_t        used;
    pthread_mutex_t lock;
} sim_queue_t;

static sim_queue_t queues[MAX_QUEUES];
static uint32_t queue_count;

rtems_status_code rtems_message_queue_create(rtems_name name, uint32_t count,
                                             size_t max_message_size,
                                             rtems_attribute attributes,
                                             rtems_id *id)
{
    sim_queue_t *q;
    (void)name; (void)attributes;
    if (queue_count >= MAX_QUEUES) {
        return RTEMS_TOO_MANY;
    }
    q = &queues[queue_count];
    q->storage  = malloc((size_t)count * max_message_size);
    q->sizes    = malloc((size_t)count * sizeof(size_t));
    q->capacity = count;
    q->max_size = max_message_size;
    pthread_mutex_init(&q->lock, NULL);
    if (q->storage == NULL || q->sizes == NULL) {
        return RTEMS_UNSATISFIED;
    }
    *id = queue_count++ + 1u;
    return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_message_queue_send(rtems_id id, const void *buffer,
                                           size_t size)
{
    sim_queue_t *q;
    uint32_t slot;
    if (id == 0u || id > queue_count) {
        return RTEMS_INVALID_ID;
    }
    q = &queues[id - 1u];
    if (size > q->max_size) {
        return RTEMS_INVALID_SIZE;
    }
    pthread_mutex_lock(&q->lock);
    if (q->used == q->capacity) {
        pthread_mutex_unlock(&q->lock);
        return RTEMS_TOO_MANY;
    }
    slot = (q->head + q->used) % q->capacity;
    memcpy(&q->storage[(size_t)slot * q->max_size], buffer, size);
    q->sizes[slot] = size;
    q->used++;
    pthread_mutex_unlock(&q->lock);
    return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_message_queue_receive(rtems_id id, void *buffer,
                                              size_t *size,
                                              rtems_option options,
                                              rtems_interval timeout)
{
    sim_queue_t *q;
    uint64_t deadline = rtems_clock_get_uptime_nanoseconds() +
                        (uint64_t)timeout * TICK_NS;
    if (id == 0u || id > queue_count) {
        return RTEMS_INVALID_ID;
    }
    q = &queues[id - 1u];
    for (;;) {
        pthread_mutex_lock(&q->lock);
        if (q->used > 0u) {
            *size = q->sizes[q->head];
            memcpy(buffer, &q->storage[(size_t)q->head * q->max_size], *size);
            q->head = (q->head + 1u) % q->capacity;
            q->used--;
            pthread_mutex_unlock(&q->lock);
            return RTEMS_SUCCESSFUL;
        }
        pthread_mutex_unlock(&q->lock);
        if (options & RTEMS_NO_WAIT) {
            return RTEMS_UNSATISFIED;
        }
        if (timeout != RTEMS_NO_TIMEOUT &&
            rtems_clock_get_uptime_nanoseconds() >= deadline) {
            return RTEMS_TIMEOUT;
        }
        restart_point();
        real_sleep(SLICE_NS / 4u);
    }
}

/* ---- Semaphores -------------------------------------------------------------- */

typedef struct {
    pthread_mutex_t lock;
    uint32_t        count;
} sim_sem_t;

static sim_sem_t sems[MAX_SEMS];
static uint32_t sem_count;

rtems_status_code rtems_semaphore_create(rtems_name name, uint32_t count,
                                         rtems_attribute attributes,
                                         rtems_task_priority ceiling,
                                         rtems_id *id)
{
    (void)name; (void)attributes; (void)ceiling;
    pthread_mutex_lock(&table_lock);
    if (sem_count >= MAX_SEMS) {
        pthread_mutex_unlock(&table_lock);
        return RTEMS_TOO_MANY;
    }
    pthread_mutex_init(&sems[sem_count].lock, NULL);
    sems[sem_count].count = count;
    *id = sem_count++ + 1u;
    pthread_mutex_unlock(&table_lock);
    return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_semaphore_obtain(rtems_id id, rtems_option options,
                                         rtems_interval timeout)
{
    sim_sem_t *s;
    uint64_t deadline = rtems_clock_get_uptime_nanoseconds() +
                        (uint64_t)timeout * TICK_NS;
    if (id == 0u || id > sem_count) {
        return RTEMS_INVALID_ID;
    }
    s = &sems[id - 1u];
    for (;;) {
        pthread_mutex_lock(&s->lock);
        if (s->count > 0u) {
            s->count--;
            pthread_mutex_unlock(&s->lock);
            return RTEMS_SUCCESSFUL;
        }
        pthread_mutex_unlock(&s->lock);
        if (options & RTEMS_NO_WAIT) {
            return RTEMS_UNSATISFIED;
        }
        if (timeout != RTEMS_NO_TIMEOUT &&
            rtems_clock_get_uptime_nanoseconds() >= deadline) {
            return RTEMS_TIMEOUT;
        }
        restart_point();
        real_sleep(SLICE_NS / 8u);
    }
}

rtems_status_code rtems_semaphore_release(rtems_id id)
{
    if (id == 0u || id > sem_count) {
        return RTEMS_INVALID_ID;
    }
    pthread_mutex_lock(&sems[id - 1u].lock);
    sems[id - 1u].count++;
    pthread_mutex_unlock(&sems[id - 1u].lock);
    return RTEMS_SUCCESSFUL;
}

/* ---- Timers (the "clock tick interrupt") ---------------------------------- */

typedef struct {
    int                               armed;
    uint64_t                          due_ns;
    rtems_timer_service_routine_entry routine;
    void                             *data;
} sim_timer_t;

static sim_timer_t timers[MAX_TIMERS];
static uint32_t timer_count;
static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;

rtems_status_code rtems_timer_create(rtems_name name, rtems_id *id)
{
    (void)name;
    if (timer_count >= MAX_TIMERS) {
        return RTEMS_TOO_MANY;
    }
    *id = timer_count++ + 1u;
    return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_timer_fire_after(rtems_id id, rtems_interval ticks,
                                         rtems_timer_service_routine_entry r,
                                         void *user_data)
{
    if (id == 0u || id > timer_count) {
        return RTEMS_INVALID_ID;
    }
    pthread_mutex_lock(&timer_lock);
    timers[id - 1u].due_ns  = rtems_clock_get_uptime_nanoseconds() +
                              (uint64_t)ticks * TICK_NS;
    timers[id - 1u].routine = r;
    timers[id - 1u].data    = user_data;
    timers[id - 1u].armed   = 1;
    pthread_mutex_unlock(&timer_lock);
    return RTEMS_SUCCESSFUL;
}

static void *timer_thread(void *p)
{
    (void)p;
    for (;;) {
        uint32_t i;
        uint64_t now = rtems_clock_get_uptime_nanoseconds();
        for (i = 0; i < timer_count; i++) {
            rtems_timer_service_routine_entry r = NULL;
            void *data = NULL;
            pthread_mutex_lock(&timer_lock);
            if (timers[i].armed && timers[i].due_ns <= now) {
                timers[i].armed = 0;
                r = timers[i].routine;
                data = timers[i].data;
            }
            pthread_mutex_unlock(&timer_lock);
            if (r != NULL) {
                /* Like an ISR: no task runs "inside" interrupt disable. */
                hostsim_interrupt_disable();
                r(i + 1u, data);
                hostsim_interrupt_enable();
            }
        }
        real_sleep(SLICE_NS / 2u);
    }
    return NULL;
}

/* ---- Entry point ----------------------------------------------------------- */

static void *init_trampoline(void *p)
{
    (void)p;
    Init(0);
    return NULL;
}

int main(void)
{
    pthread_t init_thread, tick_thread;
    pthread_mutexattr_t attr;
    const char *s = getenv("HOSTSIM_SPEEDUP");

    if (s != NULL && atoi(s) > 0) {
        speedup = (uint64_t)atoi(s);
    }
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&isr_lock, &attr);

    boot_real_ns = real_ns();
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("*** HOST SIMULATION (POSIX shim, not RTEMS), speedup x%llu ***\n",
           (unsigned long long)speedup);
    fflush(stdout);

    pthread_create(&tick_thread, NULL, timer_thread, NULL);
    pthread_create(&init_thread, NULL, init_trampoline, NULL);
    pthread_exit(NULL);
}
