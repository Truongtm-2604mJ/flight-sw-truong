/*
 * Session 5 application - identifiers and constants shared by the
 * onboard software, the tests and (by hand) the ground station in
 * ../lab-5.4-fault-injection/ground/fdir.py.
 */
#ifndef FDIR_APP_H
#define FDIR_APP_H

/* ---- APIDs (unchanged from Session 4) ------------------------------------ */
#define APID_CMD            0x010u   /* TC in; TM(1,x), TM(17,x)          */
#define APID_HK             0x020u   /* TM(3,25)                           */
#define APID_EVENT          0x021u   /* TM(5,x)                            */

/* ---- Monitored parameters --------------------------------------------------- */
#define PARAM_VBUS          1u       /* bus voltage, mV                    */
#define PARAM_TBAT          2u       /* battery temperature, 0.01 C        */
#define PARAM_TOBC          3u       /* OBC temperature, 0.01 C            */
#define NUM_MONITORS        3u

/* ---- Supervised tasks (index into the supervisor table) ---------------------- */
#define TASK_ACS            0u
#define TASK_HK             1u
#define TASK_LOG            2u
#define NUM_SUPERVISED      3u

/* ---- Event IDs (TM(5,x) auxiliary data starts with the event ID) ------------ */
#define EVT_MON_VBUS        0x0210u
#define EVT_MON_TBAT        0x0220u
#define EVT_MON_TOBC        0x0230u
/* 0x0301..0x0305 liveness events: see supervisor.h (SUP_EVT_*) */
#define EVT_MODE_CHANGED    0x0401u
#define EVT_INHIBIT_CHANGED 0x0402u
#define EVT_SCHED_SUSPENDED 0x0403u
#define EVT_BOOT_REPORT     0x0501u
#define EVT_WDG_WARNING     0x0502u

/* ACTION_INHIBITED (0x0304) "kind" field */
#define INHIBITED_RESTART   1u
#define INHIBITED_MODE      2u

/* ---- Telecommand services ------------------------------------------------------ */
#define SVC_FUNCTION        8u       /* TC(8,1) perform function            */
#define FUNC_SET_MODE       1u       /*   app data: func u8, mode u8        */
#define SVC_SCHEDULE        11u      /* TC(11,1/2/3/4)                      */
#define SVC_MONITORING      12u      /* TC(12,1/2) enable / disable         */
#define SVC_EVENT_ACTION    19u      /* TC(19,8/9) FDIR actions on / off    */
#define SVC_FAULT_INJECT    200u     /* test builds only                    */

/* ---- Housekeeping structures ------------------------------------------------------ */
#define HK_SID_FULL         1u
#define HK_SID_ESSENTIAL    2u
#define HK_SID1_LEN         37u
#define HK_SID2_LEN         17u

#define ST_FDIR_INHIBITED   0x01u    /* status byte bits                    */
#define ST_PAYLOAD_ON       0x02u
#define ST_SCHED_ENABLED    0x04u
#define ST_RX_ON            0x08u
#define ST_SUN_POINTING     0x10u
#define ST_WDG_WARNING      0x20u

/* ---- Timing (1 tick = 10 ms) ------------------------------------------------------- */
#define ACS_PERIOD_TICKS    10u      /*  100 ms */
#define HK_PERIOD_TICKS     50u      /*  500 ms */
#define LOG_PERIOD_TICKS    100u     /* 1000 ms */
#define SUP_PERIOD_TICKS    200u     /* 2000 ms */
#define SCHED_PERIOD_TICKS  100u     /* 1000 ms */

#define SUP_CHECK_MS        2000u
#define SUP_WINDOW_MS       300000u  /* 5 minutes */
#define SUP_MAX_RESTARTS_N  3u
#define WDG_TIMEOUT_MS      5000u
#define WDG_GRACE_MS        1000u
#define WDG_TICK_TICKS      10u      /* the "hardware" counts in 100 ms steps */
#define WDG_TICK_MS         100u

#define FDIR_FATAL_WATCHDOG 0x57444F47u   /* "WDOG" */

/* ---- Priorities (smaller = higher) ------------------------------------------------- */
#define SUP_PRIORITY        5
#define ACS_PRIORITY        10
#define HK_PRIORITY         20
#define LOG_PRIORITY        30
#define TC_RX_PRIORITY      40
#define SCHED_PRIORITY      45
#define TM_TX_PRIORITY      50

#endif /* FDIR_APP_H */
