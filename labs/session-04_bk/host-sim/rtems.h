/*
 * HOST SIMULATION ONLY - not flight code, not RTEMS.
 *
 * A small POSIX implementation of the part of the RTEMS Classic API the
 * Session 4 labs use, so the unmodified lab sources (lab4-2.c, lab4-3.c,
 * tmtc_tasks.c) can be built with the host gcc and exercised by the
 * ground scripts without a cross toolchain or a simulator.
 *
 * What it does NOT reproduce: priorities and preemption. Every "task"
 * is an ordinary pthread, so nothing here says anything about
 * scheduling or timing on the target. Timing evidence must come from
 * RTEMS on SIS.
 */
#ifndef HOST_SIM_RTEMS_H
#define HOST_SIM_RTEMS_H

#include <stdint.h>
#include <stddef.h>

typedef uint32_t  rtems_id;
typedef uint32_t  rtems_name;
typedef uint32_t  rtems_interval;
typedef uint32_t  rtems_task_priority;
typedef uint32_t  rtems_mode;
typedef uint32_t  rtems_attribute;
typedef uint32_t  rtems_option;
typedef uintptr_t rtems_task_argument;
typedef void      rtems_task;
typedef rtems_task (*rtems_task_entry)(rtems_task_argument);

typedef enum {
    RTEMS_SUCCESSFUL = 0,
    RTEMS_INVALID_ID = 4,
    RTEMS_TOO_MANY = 5,
    RTEMS_TIMEOUT = 6,
    RTEMS_INVALID_SIZE = 8,
    RTEMS_UNSATISFIED = 13
} rtems_status_code;

#define RTEMS_MINIMUM_STACK_SIZE  4096u
#define RTEMS_DEFAULT_MODES       0u
#define RTEMS_DEFAULT_ATTRIBUTES  0u
#define RTEMS_FLOATING_POINT      1u
#define RTEMS_FIFO                0u
#define RTEMS_WAIT                0u
#define RTEMS_NO_WAIT             1u
#define RTEMS_NO_TIMEOUT          0u

#define rtems_build_name(a, b, c, d) \
    (((rtems_name)(a) << 24) | ((rtems_name)(b) << 16) | \
     ((rtems_name)(c) << 8) | (rtems_name)(d))

const char *rtems_status_text(rtems_status_code sc);

rtems_status_code rtems_task_create(rtems_name name,
                                    rtems_task_priority priority,
                                    size_t stack_size, rtems_mode modes,
                                    rtems_attribute attributes, rtems_id *id);
rtems_status_code rtems_task_start(rtems_id id, rtems_task_entry entry,
                                   rtems_task_argument arg);
rtems_status_code rtems_task_wake_after(rtems_interval ticks);
void rtems_task_exit(void);
void rtems_shutdown_executive(uint32_t result);

rtems_status_code rtems_rate_monotonic_create(rtems_name name, rtems_id *id);
rtems_status_code rtems_rate_monotonic_period(rtems_id id,
                                              rtems_interval length);
void rtems_rate_monotonic_report_statistics(void);

rtems_status_code rtems_message_queue_create(rtems_name name, uint32_t count,
                                             size_t max_message_size,
                                             rtems_attribute attributes,
                                             rtems_id *id);
rtems_status_code rtems_message_queue_send(rtems_id id, const void *buffer,
                                           size_t size);
rtems_status_code rtems_message_queue_receive(rtems_id id, void *buffer,
                                              size_t *size,
                                              rtems_option options,
                                              rtems_interval timeout);

uint64_t rtems_clock_get_uptime_nanoseconds(void);

/* The application's Init task, as declared by the lab source. */
rtems_task Init(rtems_task_argument arg);

#endif /* HOST_SIM_RTEMS_H */
