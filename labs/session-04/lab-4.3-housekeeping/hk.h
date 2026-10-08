/*
 * Lab 4.3 - Housekeeping reporting (PUS service 3) and event reporting
 * (PUS service 5) for the platform parameter set.
 *
 * Portable C99. hk_cycle() is called once per period by the rate
 * monotonic TM_HK task; everything it needs from the system arrives
 * through hk_ctx_t, so the same code runs in the host unit tests.
 *
 * Threading: hk_ctx_t is owned by the TM_HK task, which is therefore the
 * only writer of the two sequence counters. The single field shared with
 * another task is `enabled`, written by the command task and read here
 * as one 32 bit word.
 */
#ifndef HK_H
#define HK_H

#include <stdint.h>
#include <stddef.h>

#include "pus.h"
#include "tc_handler.h"
#include "limit_mon.h"
#include "sensors_sim.h"

/* Application process identifiers owned by the TM_HK task. */
#define APID_HK     0x020u
#define APID_EVENT  0x021u

/* PUS service 3: housekeeping. */
#define PUS_SVC_HK                 3u
#define PUS_TC_3_ENABLE_PERIODIC   5u
#define PUS_TC_3_DISABLE_PERIODIC  6u
#define PUS_TM_3_HK_REPORT         25u

/* PUS service 5: event reporting. */
#define PUS_SVC_EVENT              5u
#define PUS_TM_5_INFORMATIVE       1u
#define PUS_TM_5_LOW_SEVERITY      2u

/* Housekeeping parameter report structure 1: platform parameters.
 *
 *  offset  size  parameter       type    unit
 *     0     1    SID             u8      = 1
 *     1     2    V_BUS           u16     mV
 *     3     2    T_BAT           i16     0.01 deg C
 *     5     2    T_OBC           i16     0.01 deg C
 *     7     1    MODE            u8      bit 0: periodic HK enabled
 *                                        bit 1: T_BAT monitor in HIGH state
 *     8     2    TC_REJECTED     u16     error counter, wraps
 *    10     2    TC_ACCEPTED     u16     wraps
 *    12     2    TM_DROPPED      u16     TM queue overflows, wraps
 *    14     4    UPTIME          u32     s since start (onboard time)
 *    18     2    HK_EXEC         u16     us spent in the previous cycle's
 *                                        TM work, saturates at 65535
 */
#define HK_SID_PLATFORM   1u
#define HK_REPORT_LEN     20u

#define HK_MODE_ENABLED   0x01u
#define HK_MODE_TBAT_HIGH 0x02u

/* Event definitions. */
#define EVT_TEMP_BAT_HIGH     0x0101u  /* TM(5,2): rose above the limit   */
#define EVT_TEMP_BAT_NOMINAL  0x0102u  /* TM(5,1): back inside the limits */
#define EVT_AUX_LEN           7u       /* event ID, param ID, value, limit */
#define PARAM_ID_T_BAT        2u

/* Monitoring definition for the battery temperature. */
#define TBAT_LIMIT_CDEG       4500     /* 45.00 C */
#define TBAT_HYSTERESIS_CDEG  200      /*  2.00 C > 1.60 C peak to peak noise */

/* One acquisition: values plus the time they were sampled. */
typedef struct {
    obt_t    time;
    uint16_t vbus_mv;
    int16_t  t_bat_cdeg;
    int16_t  t_obc_cdeg;
    uint8_t  mode;
    uint16_t tc_rejected;
    uint16_t tc_accepted;
    uint16_t tm_dropped;
    uint32_t uptime_s;
    uint16_t hk_exec_us;
} hk_sample_t;

typedef struct {
    tm_source_t   hk_src;       /* APID_HK and its sequence counter    */
    tm_source_t   event_src;    /* APID_EVENT and its sequence counter */
    tm_sink_fn    sink;
    void         *sink_arg;
    obt_now_fn    now;
    const tc_ctx_t *tc;         /* for the TC counters (read only)     */
    uint32_t    (*tm_dropped)(void);
    sensors_sim_t sensors;
    limit_mon_t   tbat_mon;
    volatile uint32_t enabled;  /* periodic generation on/off          */
    uint16_t      last_exec_us; /* set by the task around hk_cycle()   */
} hk_ctx_t;

void hk_init(hk_ctx_t *hk, tm_sink_fn sink, void *sink_arg, obt_now_fn now,
             const tc_ctx_t *tc, uint32_t (*tm_dropped)(void));

/* Register TC(3,5) and TC(3,6) with the telecommand handler. */
int hk_register_service(hk_ctx_t *hk, tc_ctx_t *tc);

/* One housekeeping cycle: sample, monitor, report. Never blocks. */
void hk_cycle(hk_ctx_t *hk);

/* Exposed for the unit tests. */
void hk_pack(const hk_sample_t *s, uint8_t out[HK_REPORT_LEN]);

#endif /* HK_H */
