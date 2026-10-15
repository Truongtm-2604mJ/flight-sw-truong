/*
 * Session 5 TM/TC chain: the Session 4 TC_RX / TM_TX tasks and TM queue
 * (lab-4.2-pus-service17/tmtc_tasks.c), with one change: telecommands
 * now arrive from two tasks (TC_RX from the ground, SCHED from the
 * onboard schedule), so tc_handle() and everything it touches run under
 * one mutex with priority inheritance.
 *
 * Lock order in the whole application: tc lock -> mode lock -> event
 * lock. Nothing takes them in another order.
 */
#ifndef TMTC5_H
#define TMTC5_H

#include <rtems.h>

#include "tc_handler.h"

#define TMTC_TASK_STACK  (8u * 1024u)
#define TMTC_TASK_COUNT  2u
#define TM_QUEUE_DEPTH   48u

rtems_status_code tmtc_init(void);
rtems_status_code tmtc_start_tasks(void);
tc_ctx_t *tmtc_tc_ctx(void);

/* Process one telecommand frame under the tc lock. */
void tmtc_dispatch(const uint8_t *frame, size_t len);
/* For a caller that already holds the lock (the schedule). */
void tmtc_lock(void);
void tmtc_unlock(void);
void tmtc_dispatch_locked(const uint8_t *frame, size_t len);

int      tmtc_tm_sink(const uint8_t *packet, size_t len, void *arg);
obt_t    tmtc_now(void);
uint32_t tmtc_tm_dropped(void);

#endif /* TMTC5_H */
