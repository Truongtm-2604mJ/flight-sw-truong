/*
 * Session 5 application - FDIR glue: wires the portable Lab 5.1 to 5.3
 * modules to the RTEMS tasks, the TM/TC chain and each other.
 *
 * Ownership (who writes what):
 *   monitor table, sensors, HK APID counter   TM_HK task
 *   supervisor table                          SUP task (heartbeats: each task)
 *   mode manager                              any task, under the mode lock
 *   schedule                                  under the tc lock
 *   event APID counter                        any task, under the event lock
 *   watchdog counter                          SUP (kick) and timer ISR (tick)
 *   NV record                                 any context, interrupts disabled
 */
#ifndef FDIR_H
#define FDIR_H

#include <rtems.h>
#include <stdint.h>

#include "modes.h"
#include "schedule.h"
#include "supervisor.h"

/* Boot: NV record, locks, modules, boot report, initial mode. Call
 * after tmtc_init() and before any task uses FDIR. */
rtems_status_code fdir_boot(void);

/* Register a supervised task (before the supervisor starts). */
int fdir_supervise(const char *name, uint32_t period_ms, rtems_id id,
                   vmode_t escalation);

void fdir_watchdog_start(void);

/* Task bodies. */
void fdir_heartbeat(uint32_t task_idx);
void fdir_hk_cycle(void);
void fdir_supervisor_cycle(void);
void fdir_sched_cycle(void);

int  fdir_payload_on(void);

/* Used by the telecommand services. */
mode_result_t fdir_mode_request(vmode_t target, cause_src_t src,
                                uint16_t detail);
int      fdir_set_inhibit(int inhibit, uint16_t duration_s);
int      fdir_param_known(uint16_t param_id);
void     fdir_monitor_enable(uint16_t param_id, int enable); /* next HK cycle */
sched_t *fdir_schedule(void);                                /* tc lock held */
vmode_t  fdir_mode(void);

#endif /* FDIR_H */
