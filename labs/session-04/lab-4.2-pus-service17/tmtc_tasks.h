/*
 * Lab 4.2 - RTEMS side of the TM/TC chain: the ground link, the TM
 * queue and the two tasks around the portable telecommand handler.
 *
 *                 +---------+  frame   +------------+
 *   console RX -->|  TC_RX  |--------->| tc_handle  |--+
 *                 +---------+          +------------+  | TM packets
 *                                                      v
 *                 +---------+   RTEMS message queue  +-----+
 *   console TX <--|  TM_TX  |<-----------------------| TMQ |<-- other
 *                 +---------+                        +-----+    tasks
 *
 * Link framing (lab simplification, see design note): one packet per
 * text line, hex encoded, with a direction prefix.
 *     ground -> spacecraft   "TC:<hex octets>\n"
 *     spacecraft -> ground   "TM:<hex octets>\n"
 * Any console line without the prefix is ordinary console text and is
 * ignored by both ends, so the link can share the console UART with the
 * boot banner and works unchanged on SIS, QEMU or a real serial port.
 */
#ifndef TMTC_TASKS_H
#define TMTC_TASKS_H

#include <rtems.h>

#include "tc_handler.h"

/* Application process identifiers (see allocation table, design note). */
#define APID_CMD     0x010u  /* TC destination; TM(1,x) and TM(17,x)   */

/* Priorities: both tasks sit BELOW every periodic task, so command
 * handling and downlink only ever use slack and stay out of the rate
 * monotonic analysis. Smaller number = higher priority. */
#define TC_RX_PRIORITY   40
#define TM_TX_PRIORITY   50

#define TMTC_TASK_STACK  (8u * 1024u)
#define TMTC_TASK_COUNT  2u

/* TM queue depth, in packets. Sized for 2 Hz housekeeping plus bursts
 * of verification reports with a slow console. */
#define TM_QUEUE_DEPTH   32u

/* Create the TM queue and the telecommand context. Call from Init. */
rtems_status_code tmtc_init(void);

/* The telecommand context, for registering services before start. */
tc_ctx_t *tmtc_tc_ctx(void);

/* Create and start TC_RX and TM_TX. */
rtems_status_code tmtc_start_tasks(void);

/* Queue one TM packet for downlink. Never blocks; safe from any task.
 * Returns 0, or non zero if the queue was full and the packet dropped.
 * Signature matches tm_sink_fn. */
int tmtc_tm_sink(const uint8_t *packet, size_t len, void *arg);

/* Onboard time: CUC seconds + 1/65536 s since application start. */
obt_t tmtc_now(void);

/* Number of TM packets dropped because the queue was full. */
uint32_t tmtc_tm_dropped(void);

#endif /* TMTC_TASKS_H */
