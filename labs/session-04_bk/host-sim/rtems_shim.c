/* HOST SIMULATION ONLY - see rtems.h in this directory. */
#define _POSIX_C_SOURCE 200809L

#include "rtems.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TICK_NS        10000000ull /* matches CONFIGURE_MICROSECONDS_PER_TICK */
#define MAX_TASKS      16
#define MAX_PERIODS    8
#define MAX_QUEUES     4

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void sleep_until(uint64_t t_ns)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(t_ns / 1000000000ull);
    ts.tv_nsec = (long)(t_ns % 1000000000ull);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) != 0) {
    }
}

static uint64_t boot_ns;

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

uint64_t rtems_clock_get_uptime_nanoseconds(void)
{
    return now_ns() - boot_ns;
}

/* ---- Tasks ---------------------------------------------------------------- */

typedef struct {
    rtems_task_entry    entry;
    rtems_task_argument arg;
    pthread_t           thread;
} sim_task_t;

static sim_task_t tasks[MAX_TASKS];
static uint32_t task_count;
static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;

static void *task_trampoline(void *p)
{
    sim_task_t *t = p;
    t->entry(t->arg);
    return NULL;
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
    *id = task_count++;
    pthread_mutex_unlock(&table_lock);
    return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_task_start(rtems_id id, rtems_task_entry entry,
                                   rtems_task_argument arg)
{
    if (id >= task_count) {
        return RTEMS_INVALID_ID;
    }
    tasks[id].entry = entry;
    tasks[id].arg   = arg;
    if (pthread_create(&tasks[id].thread, NULL, task_trampoline,
                       &tasks[id]) != 0) {
        return RTEMS_UNSATISFIED;
    }
    return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_task_wake_after(rtems_interval ticks)
{
    sleep_until(now_ns() + (uint64_t)ticks * TICK_NS);
    return RTEMS_SUCCESSFUL;
}

void rtems_task_exit(void)
{
    pthread_exit(NULL);
}

void rtems_shutdown_executive(uint32_t result)
{
    fflush(stdout);
    exit((int)result);
}

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
    *id = period_count++;
    pthread_mutex_unlock(&table_lock);
    return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_rate_monotonic_period(rtems_id id,
                                              rtems_interval length)
{
    sim_period_t *p;
    uint64_t now = now_ns();

    if (id >= period_count) {
        return RTEMS_INVALID_ID;
    }
    p = &periods[id];

    if (!p->started) {
        p->started = 1;
        p->deadline_ns = now + (uint64_t)length * TICK_NS;
        return RTEMS_SUCCESSFUL;
    }

    p->count++;
    if (now > p->deadline_ns) {
        p->missed++;
        p->deadline_ns = now + (uint64_t)length * TICK_NS;
        return RTEMS_TIMEOUT;
    }

    sleep_until(p->deadline_ns);
    p->deadline_ns += (uint64_t)length * TICK_NS;
    return RTEMS_SUCCESSFUL;
}

void rtems_rate_monotonic_report_statistics(void)
{
    uint32_t i;
    printf("HOST SIMULATION - period counts only, no timing data\n");
    printf("  NAME  COUNT  MISSED\n");
    for (i = 0; i < period_count; i++) {
        rtems_name n = periods[i].name;
        printf("  %c%c%c%c  %5u  %6u\n",
               (char)(n >> 24), (char)(n >> 16), (char)(n >> 8), (char)n,
               (unsigned)periods[i].count, (unsigned)periods[i].missed);
    }
}

/* ---- Message queues --------------------------------------------------------- */

typedef struct {
    uint8_t        *storage;
    size_t         *sizes;
    uint32_t        capacity;
    size_t          max_size;
    uint32_t        head;
    uint32_t        used;
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
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
    q->head = 0;
    q->used = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    if (q->storage == NULL || q->sizes == NULL) {
        return RTEMS_UNSATISFIED;
    }
    *id = queue_count++;
    return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_message_queue_send(rtems_id id, const void *buffer,
                                           size_t size)
{
    sim_queue_t *q;
    uint32_t slot;

    if (id >= queue_count) {
        return RTEMS_INVALID_ID;
    }
    q = &queues[id];
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
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_message_queue_receive(rtems_id id, void *buffer,
                                              size_t *size,
                                              rtems_option options,
                                              rtems_interval timeout)
{
    sim_queue_t *q;

    (void)timeout; /* only RTEMS_NO_TIMEOUT is used by the labs */
    if (id >= queue_count) {
        return RTEMS_INVALID_ID;
    }
    q = &queues[id];

    pthread_mutex_lock(&q->lock);
    while (q->used == 0) {
        if (options & RTEMS_NO_WAIT) {
            pthread_mutex_unlock(&q->lock);
            return RTEMS_UNSATISFIED;
        }
        pthread_cond_wait(&q->not_empty, &q->lock);
    }
    *size = q->sizes[q->head];
    memcpy(buffer, &q->storage[(size_t)q->head * q->max_size], *size);
    q->head = (q->head + 1u) % q->capacity;
    q->used--;
    pthread_mutex_unlock(&q->lock);
    return RTEMS_SUCCESSFUL;
}

/* ---- Entry point ------------------------------------------------------------- */

static void *init_trampoline(void *p)
{
    (void)p;
    Init(0);
    return NULL;
}

int main(void)
{
    pthread_t init_thread;

    boot_ns = now_ns();
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("*** HOST SIMULATION (POSIX shim, not RTEMS) ***\n");
    fflush(stdout);

    pthread_create(&init_thread, NULL, init_trampoline, NULL);
    pthread_exit(NULL); /* keep the process alive for the tasks */
}
