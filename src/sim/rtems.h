/*
 * HOST SIMULATION ONLY - not flight code, not RTEMS. See README.md.
 *
 * The subset of the RTEMS Classic API used by the Session 5 application.
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
typedef uint32_t  rtems_interrupt_level;
typedef uint32_t  rtems_fatal_code;
typedef uintptr_t rtems_task_argument;
typedef void      rtems_task;
typedef rtems_task (*rtems_task_entry)(rtems_task_argument);
typedef void      rtems_timer_service_routine;
typedef rtems_timer_service_routine (*rtems_timer_service_routine_entry)(
    rtems_id, void *);

typedef enum {
    RTEMS_SUCCESSFUL   = 0,
    RTEMS_INVALID_ID   = 4,
    RTEMS_TOO_MANY     = 5,
    RTEMS_TIMEOUT      = 6,
    RTEMS_INVALID_SIZE = 8,
    RTEMS_UNSATISFIED  = 13
} rtems_status_code;

typedef enum { RTEMS_FATAL_SOURCE_APPLICATION = 0 } rtems_fatal_source;

#define RTEMS_MINIMUM_STACK_SIZE        4096u
#define RTEMS_DEFAULT_MODES             0u
#define RTEMS_DEFAULT_ATTRIBUTES        0u
#define RTEMS_FLOATING_POINT            1u
#define RTEMS_FIFO                      0u
#define RTEMS_PRIORITY                  0u
#define RTEMS_BINARY_SEMAPHORE          0x10u
#define RTEMS_SIMPLE_BINARY_SEMAPHORE   0x20u
#define RTEMS_COUNTING_SEMAPHORE        0u
#define RTEMS_INHERIT_PRIORITY          0u
#define RTEMS_WAIT                      0u
#define RTEMS_NO_WAIT                   1u
#define RTEMS_NO_TIMEOUT                0u

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
rtems_status_code rtems_task_restart(rtems_id id, rtems_task_argument arg);
rtems_status_code rtems_task_wake_after(rtems_interval ticks);
void rtems_task_exit(void);
void rtems_shutdown_executive(uint32_t result);
void rtems_fatal(rtems_fatal_source source, rtems_fatal_code code);

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

rtems_status_code rtems_semaphore_create(rtems_name name, uint32_t count,
                                         rtems_attribute attributes,
                                         rtems_task_priority ceiling,
                                         rtems_id *id);
rtems_status_code rtems_semaphore_obtain(rtems_id id, rtems_option options,
                                         rtems_interval timeout);
rtems_status_code rtems_semaphore_release(rtems_id id);

rtems_status_code rtems_timer_create(rtems_name name, rtems_id *id);
rtems_status_code rtems_timer_fire_after(rtems_id id, rtems_interval ticks,
                                         rtems_timer_service_routine_entry r,
                                         void *user_data);

uint64_t rtems_clock_get_uptime_nanoseconds(void);

void hostsim_interrupt_disable(void);
void hostsim_interrupt_enable(void);
#define rtems_interrupt_local_disable(level) \
    do { (void)(level); hostsim_interrupt_disable(); } while (0)
#define rtems_interrupt_local_enable(level) \
    do { (void)(level); hostsim_interrupt_enable(); } while (0)

rtems_task Init(rtems_task_argument arg);

#endif /* HOST_SIM_RTEMS_H */
