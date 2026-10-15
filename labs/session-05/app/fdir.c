/* Session 5 application - FDIR glue. See fdir.h. */
#include "fdir.h"

#include <stdio.h>
#include <string.h>

#include "fdir_app.h"
#include "finj.h"
#include "monitor.h"
#include "nvstate.h"
#include "pus.h"
#include "sensors5.h"
#include "tmtc5.h"
#include "watchdog.h"

static rtems_id         mode_lock;
static rtems_id         event_lock;
static rtems_id         wdg_timer;
static mode_mgr_t       g_mode;
static supervisor_t     g_sup;
static wdg_t            g_wdg;
static nv_record_t      g_nv;
static sched_t          g_sched;
static monitor_entry_t  g_mon[NUM_MONITORS];
static volatile uint8_t mon_request[NUM_MONITORS];   /* 0, 1 on, 2 off */
static sensors5_t       g_sens;
static int32_t          g_values[NUM_MONITORS];
static tm_source_t      hk_src  = { APID_HK, 0 };
static tm_source_t      evt_src = { APID_EVENT, 0 };

static volatile uint8_t payload_on;
static volatile uint8_t sun_pointing;
static volatile uint8_t wdg_warning_pending;
static volatile uint8_t wdg_warning_sent;

/* ---- Helpers ------------------------------------------------------------------ */

static uint32_t now_ms(void)
{
    return (uint32_t)(rtems_clock_get_uptime_nanoseconds() / 1000000u);
}

static void lock(rtems_id id)
{
    (void)rtems_semaphore_obtain(id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
}

static void unlock(rtems_id id)
{
    (void)rtems_semaphore_release(id);
}

static uint8_t sat8(uint32_t v)   { return (uint8_t)(v > 255u ? 255u : v); }
static uint16_t sat16(uint32_t v) { return (uint16_t)(v > 65535u ? 65535u : v); }

static void put_i16(uint8_t *p, int32_t v)
{
    if (v > 32767) { v = 32767; }
    if (v < -32768) { v = -32768; }
    pus_put_u16(p, (uint16_t)(int16_t)v);
}

/* ---- Non volatile record ------------------------------------------------------ */

#ifdef __rtems__
/*
 * Stands in for the non volatile memory (EEPROM / MRAM / a no-init RAM
 * area on real hardware). On SIS a reset ends the simulation, so this
 * array never actually survives one; see the design note.
 */
static uint8_t nv_memory[NV_IMAGE_LEN];
static void nv_read(uint8_t img[NV_IMAGE_LEN])        { memcpy(img, nv_memory, NV_IMAGE_LEN); }
static void nv_write(const uint8_t img[NV_IMAGE_LEN]) { memcpy(nv_memory, img, NV_IMAGE_LEN); }
#else
#include <stdlib.h>
/* Host simulation: a file, which does survive the process "reset". */
static const char *nv_path(void)
{
    const char *p = getenv("FDIR_NV_FILE");
    return p != NULL ? p : "fdir_nv.bin";
}
static void nv_read(uint8_t img[NV_IMAGE_LEN])
{
    FILE *f = fopen(nv_path(), "rb");
    memset(img, 0, NV_IMAGE_LEN);
    if (f != NULL) {
        if (fread(img, 1, NV_IMAGE_LEN, f) != NV_IMAGE_LEN) {
            memset(img, 0, NV_IMAGE_LEN);   /* short file: treat as blank */
        }
        fclose(f);
    }
}
static void nv_write(const uint8_t img[NV_IMAGE_LEN])
{
    FILE *f = fopen(nv_path(), "wb");
    if (f != NULL) {
        (void)fwrite(img, 1, NV_IMAGE_LEN, f);
        fclose(f);
    }
}
#endif

/* Callable from task and interrupt context. */
static void nv_commit(void)
{
    uint8_t img[NV_IMAGE_LEN];
    rtems_interrupt_level level;

    rtems_interrupt_local_disable(level);
    nv_encode(&g_nv, img);
    nv_write(img);
    rtems_interrupt_local_enable(level);
}

/* ---- Events ----------------------------------------------------------------------- */

static void evt_emit(uint8_t severity, const uint8_t *aux, size_t len)
{
    uint8_t pkt[PUS_MAX_PACKET];
    int n;

    lock(event_lock);
    n = pus_tm_build(&evt_src, 5, severity, 0, tmtc_now(), aux, len,
                     pkt, sizeof(pkt));
    if (n > 0) {
        (void)tmtc_tm_sink(pkt, (size_t)n, NULL);
    }
    unlock(event_lock);
}

static void evt_action_inhibited(uint8_t kind, uint16_t subject)
{
    uint8_t aux[5];
    pus_put_u16(&aux[0], SUP_EVT_ACTION_INHIBITED);
    aux[2] = kind;
    pus_put_u16(&aux[3], subject);
    evt_emit(2, aux, sizeof(aux));
}

/* ---- Mode manager callbacks (called with the mode lock held) -------------------- */

static void mode_apply(void *arg, vmode_t mode)
{
    (void)arg;
    payload_on   = (mode == MODE_NOMINAL) ? 1u : 0u;
    sun_pointing = (mode == MODE_SAFE) ? 1u : 0u;

    if (mode == MODE_SAFE && g_sched.enabled) {
        uint8_t aux[3];
        sched_set_enabled(&g_sched, 0);
        pus_put_u16(&aux[0], EVT_SCHED_SUSPENDED);
        aux[2] = (uint8_t)sched_count(&g_sched);
        evt_emit(2, aux, sizeof(aux));
    }
}

static void mode_transition(void *arg, vmode_t from, vmode_t to,
                            cause_src_t src, uint16_t detail)
{
    uint8_t aux[7];
    (void)arg;
    pus_put_u16(&aux[0], EVT_MODE_CHANGED);
    aux[2] = (uint8_t)from;
    aux[3] = (uint8_t)to;
    aux[4] = (uint8_t)src;
    pus_put_u16(&aux[5], detail);
    evt_emit(to == MODE_SAFE ? 4u : (to == MODE_DEGRADED ? 3u : 1u),
             aux, sizeof(aux));

    if (to == MODE_SAFE) {
        g_nv.last_safe_src    = (uint8_t)src;
        g_nv.last_safe_detail = detail;
        nv_commit();
    }
}

static void mode_inhibit(void *arg, int inhibited, uint16_t duration_s,
                         inhibit_reason_t reason)
{
    uint8_t aux[6];
    (void)arg;
    pus_put_u16(&aux[0], EVT_INHIBIT_CHANGED);
    aux[2] = inhibited ? 1u : 0u;
    pus_put_u16(&aux[3], duration_s);
    aux[5] = (uint8_t)reason;
    evt_emit(2, aux, sizeof(aux));
}

static const mode_ops_t mode_ops = { mode_apply, mode_transition, mode_inhibit };

mode_result_t fdir_mode_request(vmode_t target, cause_src_t src,
                                uint16_t detail)
{
    mode_result_t r;
    lock(mode_lock);
    r = mode_request(&g_mode, target, src, detail, now_ms());
    unlock(mode_lock);
    return r;
}

int fdir_set_inhibit(int inhibit, uint16_t duration_s)
{
    int r;
    lock(mode_lock);
    r = mode_set_inhibit(&g_mode, inhibit, duration_s, now_ms());
    unlock(mode_lock);
    return r;
}

vmode_t fdir_mode(void)        { return g_mode.mode; }
int     fdir_payload_on(void)  { return payload_on; }
sched_t *fdir_schedule(void)   { return &g_sched; }

/* ---- Supervisor callbacks (SUP task context) ----------------------------------- */

static int sup_restart(void *arg, size_t idx, uint32_t os_id)
{
    (void)arg; (void)idx;
    return rtems_task_restart((rtems_id)os_id, 0) == RTEMS_SUCCESSFUL ? 0 : -1;
}

static void sup_escalate(void *arg, size_t idx, uint8_t target)
{
    (void)arg;
    {
        rtems_interrupt_level level;
        rtems_interrupt_local_disable(level);
        g_nv.escalation_count  = (uint16_t)(g_nv.escalation_count + 1u);
        g_nv.last_esc_task     = (uint8_t)idx;
        g_nv.last_esc_action   = target;
        g_nv.last_esc_boot     = g_nv.boot_count;
        g_nv.last_esc_uptime_s = now_ms() / 1000u;
        rtems_interrupt_local_enable(level);
    }
    nv_commit();
    if (fdir_mode_request((vmode_t)target, CAUSE_LIVENESS, (uint16_t)idx)
        == MODE_REFUSED_INHIBIT) {
        evt_action_inhibited(INHIBITED_MODE, (uint16_t)(0x100u + idx));
    }
}

static void sup_event(void *arg, uint16_t id, size_t idx, uint32_t value)
{
    uint8_t aux[4];
    uint8_t sev;
    (void)arg;

    if (id == SUP_EVT_ACTION_INHIBITED) {
        evt_action_inhibited(INHIBITED_RESTART, (uint16_t)idx);
        return;
    }
    pus_put_u16(&aux[0], id);
    aux[2] = (uint8_t)idx;
    aux[3] = sat8(value);
    sev = (id == SUP_EVT_TASK_ESCALATED) ? 4u :
          (id == SUP_EVT_TASK_RECOVERED) ? 1u : 2u;
    evt_emit(sev, aux, sizeof(aux));
}

static int sup_inhibited(void *arg)
{
    (void)arg;
    return g_mode.inhibited;
}

static const sup_ops_t sup_ops = { sup_restart, sup_escalate, sup_event,
                                   sup_inhibited };

int fdir_supervise(const char *name, uint32_t period_ms, rtems_id id,
                   vmode_t escalation)
{
    return sup_register(&g_sup, name, period_ms, (uint32_t)id,
                        (uint8_t)escalation);
}

void fdir_heartbeat(uint32_t task_idx)
{
    sup_heartbeat(&g_sup, task_idx);
}

void fdir_supervisor_cycle(void)
{
    uint32_t now = now_ms();

    sup_check(&g_sup, now);
    lock(mode_lock);
    mode_tick(&g_mode, now);
    unlock(mode_lock);
    wdg_kick(&g_wdg);
}

/* ---- Watchdog: the timer service routine stands for the hardware ---------------- */

static rtems_timer_service_routine wdg_isr(rtems_id id, void *arg)
{
    wdg_action_t a = wdg_tick(&g_wdg, WDG_TICK_MS);

    if (a == WDG_EARLY_WARNING) {
        /* Record the cause while there is still time. */
        g_nv.pending_reset = NV_RESET_WATCHDOG;
        nv_commit();
        wdg_warning_pending = 1u;
    } else if (a == WDG_RESET) {
        rtems_fatal(RTEMS_FATAL_SOURCE_APPLICATION, FDIR_FATAL_WATCHDOG);
    }
    (void)rtems_timer_fire_after(id, WDG_TICK_TICKS, wdg_isr, arg);
}

void fdir_watchdog_start(void)
{
    if (rtems_timer_create(rtems_build_name('W', 'D', 'O', 'G'), &wdg_timer)
        == RTEMS_SUCCESSFUL) {
        (void)rtems_timer_fire_after(wdg_timer, WDG_TICK_TICKS, wdg_isr, NULL);
    }
}

/* ---- Monitoring ------------------------------------------------------------------ */

static const struct {
    uint16_t param_id;
    vmode_t  on_high;
    vmode_t  on_low;
} event_actions[NUM_MONITORS] = {
    { PARAM_VBUS, MODE_DEGRADED, MODE_SAFE     },  /* low bus: power at risk */
    { PARAM_TBAT, MODE_DEGRADED, MODE_DEGRADED },
    { PARAM_TOBC, MODE_DEGRADED, MODE_DEGRADED },
};

int fdir_param_known(uint16_t param_id)
{
    return mon_find(g_mon, NUM_MONITORS, param_id) != NULL;
}

void fdir_monitor_enable(uint16_t param_id, int enable)
{
    size_t i;
    for (i = 0; i < NUM_MONITORS; i++) {
        if (g_mon[i].param_id == param_id) {
            mon_request[i] = enable ? 1u : 2u;
        }
    }
}

static int32_t get_value(uint16_t param_id, void *ctx)
{
    (void)ctx;
    return g_values[param_id - 1u];
}

static void on_monitor(const monitor_entry_t *e, mon_transition_t tr,
                       int32_t value, void *ctx)
{
    uint8_t aux[14];
    int32_t limit = 0;
    size_t i;
    (void)ctx;

    if (tr == MON_RAISED_HIGH) { limit = e->limit_high; }
    if (tr == MON_RAISED_LOW)  { limit = e->limit_low; }

    pus_put_u16(&aux[0], e->event_id);
    pus_put_u16(&aux[2], e->param_id);
    aux[4] = (uint8_t)e->state;
    aux[5] = (uint8_t)(tr == MON_RAISED_HIGH ? MON_SIDE_HIGH :
                       tr == MON_RAISED_LOW ? MON_SIDE_LOW : MON_SIDE_NONE);
    pus_put_u32(&aux[6], (uint32_t)value);
    pus_put_u32(&aux[10], (uint32_t)limit);
    evt_emit(tr == MON_CLEARED ? 1u : e->severity, aux, sizeof(aux));

    if (tr == MON_CLEARED) {
        return;          /* no automatic way back: the ground decides */
    }
    for (i = 0; i < NUM_MONITORS; i++) {
        if (event_actions[i].param_id == e->param_id) {
            vmode_t target = (tr == MON_RAISED_HIGH) ? event_actions[i].on_high
                                                     : event_actions[i].on_low;
            if (fdir_mode_request(target, CAUSE_MONITOR, e->param_id)
                == MODE_REFUSED_INHIBIT) {
                evt_action_inhibited(INHIBITED_MODE, e->param_id);
            }
        }
    }
}

/* ---- Housekeeping ------------------------------------------------------------------ */

static uint8_t status_byte(void)
{
    uint8_t st = ST_RX_ON;     /* receiver: never switched off by FDIR */
    if (g_mode.inhibited)  { st |= ST_FDIR_INHIBITED; }
    if (payload_on)        { st |= ST_PAYLOAD_ON; }
    if (g_sched.enabled)   { st |= ST_SCHED_ENABLED; }
    if (sun_pointing)      { st |= ST_SUN_POINTING; }
    if (wdg_warning_pending) { st |= ST_WDG_WARNING; }
    return st;
}

static void hk_emit(obt_t t, const uint8_t *data, size_t len)
{
    uint8_t pkt[PUS_MAX_PACKET];
    int n = pus_tm_build(&hk_src, 3, 25, 0, t, data, len, pkt, sizeof(pkt));
    if (n > 0) {
        (void)tmtc_tm_sink(pkt, (size_t)n, NULL);
    }
}

static void hk_full(obt_t t)
{
    uint8_t d[HK_SID1_LEN];
    uint8_t en = 0, states = 0, failed = 0;
    uint32_t filtered = 0;
    size_t i;
    const tc_ctx_t *tc = tmtc_tc_ctx();

    for (i = 0; i < NUM_MONITORS; i++) {
        if (g_mon[i].enabled) { en |= (uint8_t)(1u << i); }
        states |= (uint8_t)(((uint8_t)g_mon[i].state & 3u) << (2u * i));
        filtered += g_mon[i].filtered;
    }
    for (i = 0; i < g_sup.count; i++) {
        if (g_sup.tasks[i].status == SUP_TASK_FAILED) {
            failed |= (uint8_t)(1u << i);
        }
    }

    d[0] = HK_SID_FULL;
    pus_put_u16(&d[1], sat16((uint32_t)(g_values[0] < 0 ? 0 : g_values[0])));
    put_i16(&d[3], g_values[1]);
    put_i16(&d[5], g_values[2]);
    d[7] = (uint8_t)g_mode.mode;
    d[8] = (uint8_t)g_mode.last_src;
    pus_put_u16(&d[9], g_mode.last_detail);
    d[11] = status_byte();
    d[12] = en;
    d[13] = states;
    pus_put_u16(&d[14], sat16(filtered));
    d[16] = g_sup.tasks[TASK_ACS].restarts_total;
    d[17] = g_sup.tasks[TASK_HK].restarts_total;
    d[18] = g_sup.tasks[TASK_LOG].restarts_total;
    d[19] = failed;
    pus_put_u16(&d[20], g_nv.boot_count);
    d[22] = g_nv.last_reset_cause;
    d[23] = sat8(g_nv.reset_count);
    d[24] = sat8(g_nv.escalation_count);
    pus_put_u16(&d[25], (uint16_t)tc->accepted);
    pus_put_u16(&d[27], (uint16_t)tc->rejected);
    pus_put_u16(&d[29], (uint16_t)tmtc_tm_dropped());
    pus_put_u32(&d[31], t.coarse);
    pus_put_u16(&d[35], mode_inhibit_remaining_s(&g_mode, now_ms()));
    hk_emit(t, d, sizeof(d));
}

static void hk_essential(obt_t t)
{
    uint8_t d[HK_SID2_LEN];

    d[0] = HK_SID_ESSENTIAL;
    pus_put_u16(&d[1], sat16((uint32_t)(g_values[0] < 0 ? 0 : g_values[0])));
    put_i16(&d[3], g_values[1]);
    d[5] = (uint8_t)g_mode.mode;
    d[6] = (uint8_t)g_mode.last_src;
    pus_put_u16(&d[7], g_mode.last_detail);
    d[9] = status_byte();
    pus_put_u16(&d[10], g_nv.boot_count);
    d[12] = g_nv.last_reset_cause;
    pus_put_u32(&d[13], t.coarse);
    hk_emit(t, d, sizeof(d));
}

/*
 * One TM_HK period (500 ms). Monitoring and reporting rate by mode:
 *   NOMINAL   monitor and full HK every 2nd cycle (1 Hz)
 *   DEGRADED  monitor and full HK every cycle (2 Hz): "tightened"
 *   SAFE      monitor every cycle; essential HK every 4th cycle (0.5 Hz)
 */
void fdir_hk_cycle(void)
{
    static uint32_t cycle;
    vmode_t mode = g_mode.mode;
    size_t i;

    /* Ground enable/disable requests, applied by the table's owner. */
    for (i = 0; i < NUM_MONITORS; i++) {
        uint8_t r = mon_request[i];
        if (r != 0u) {
            mon_request[i] = 0;
            mon_set_enabled(&g_mon[i], r == 1u);
        }
    }

    if (wdg_warning_pending && !wdg_warning_sent) {
        uint8_t aux[6];
        wdg_warning_sent = 1u;
        pus_put_u16(&aux[0], EVT_WDG_WARNING);
        pus_put_u32(&aux[2], g_wdg.since_kick_ms);
        evt_emit(4, aux, sizeof(aux));
    }

    if (mode != MODE_NOMINAL || (cycle % 2u) == 0u) {
        sensor5_values_t v;
        obt_t t = tmtc_now();                /* stamped at acquisition */

        sensors5_sample(&g_sens, &v);
        g_values[0] = FINJ_PARAM(PARAM_VBUS, v.vbus_mv);
        g_values[1] = FINJ_PARAM(PARAM_TBAT, v.tbat_cdeg);
        g_values[2] = FINJ_PARAM(PARAM_TOBC, v.tobc_cdeg);

        mon_table_run(g_mon, NUM_MONITORS, get_value, on_monitor, NULL);

        if (g_mode.mode != MODE_SAFE) {
            hk_full(t);
        } else if ((cycle % 4u) == 0u) {
            hk_essential(t);
        }
    }
    cycle++;
}

/* ---- Schedule ---------------------------------------------------------------------- */

static void sched_release(const uint8_t *tc, size_t len, void *arg)
{
    (void)arg;
    tmtc_dispatch_locked(tc, len);
}

void fdir_sched_cycle(void)
{
    tmtc_lock();
    (void)sched_release_due(&g_sched, now_ms() / 1000u, sched_release, NULL);
    tmtc_unlock();
}

/* ---- Boot -------------------------------------------------------------------------- */

static rtems_status_code make_mutex(rtems_name name, rtems_id *id)
{
    return rtems_semaphore_create(name, 1,
                                  RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY |
                                  RTEMS_INHERIT_PRIORITY, 0, id);
}

rtems_status_code fdir_boot(void)
{
    uint8_t img[NV_IMAGE_LEN];
    uint8_t aux[10];
    int nv_valid;
    nv_reset_cause_t cause;
    vmode_t boot_mode;
    rtems_status_code sc;
    size_t i;

    sc = make_mutex(rtems_build_name('M', 'O', 'D', 'E'), &mode_lock);
    if (sc == RTEMS_SUCCESSFUL) {
        sc = make_mutex(rtems_build_name('E', 'V', 'N', 'T'), &event_lock);
    }
    if (sc != RTEMS_SUCCESSFUL) {
        return sc;
    }

    /* What happened before this boot? */
    nv_read(img);
    cause = nv_boot(&g_nv, img, &nv_valid);
    nv_commit();

    /*
     * Monitoring definitions. Rationale for every number:
     * docs/fdir-design.md, section 2.
     *               param       low    low_clr hi_clr  high  pers  event   sev */
    mon_define(&g_mon[0], PARAM_VBUS, 26000, 26300, 29200, 29500, 3, EVT_MON_VBUS, 3);
    mon_define(&g_mon[1], PARAM_TBAT,     0,   200,  4300,  4500, 3, EVT_MON_TBAT, 2);
    mon_define(&g_mon[2], PARAM_TOBC, -2000, -1500,  6500,  7000, 5, EVT_MON_TOBC, 2);
    for (i = 0; i < NUM_MONITORS; i++) {
        if (mon_check_definition(&g_mon[i]) != 0) {
            return RTEMS_UNSATISFIED;
        }
    }

    sensors5_init(&g_sens);
    sched_init(&g_sched);
    wdg_init(&g_wdg, WDG_TIMEOUT_MS, WDG_GRACE_MS);
    if (sup_init(&g_sup, SUP_CHECK_MS, SUP_WINDOW_MS, SUP_MAX_RESTARTS_N,
                 &sup_ops, NULL) != 0) {
        return RTEMS_UNSATISFIED;
    }

    /* An unplanned reset means something went wrong that the software
     * could not handle: come up in SAFE and wait for the ground. */
    boot_mode = (cause == NV_RESET_WATCHDOG) ? MODE_SAFE : MODE_NOMINAL;

    pus_put_u16(&aux[0], EVT_BOOT_REPORT);
    pus_put_u16(&aux[2], g_nv.boot_count);
    aux[4] = g_nv.last_reset_cause;
    aux[5] = sat8(g_nv.reset_count);
    aux[6] = sat8(g_nv.escalation_count);
    aux[7] = g_nv.last_esc_task;
    aux[8] = (uint8_t)boot_mode;
    aux[9] = nv_valid ? 1u : 0u;
    evt_emit(cause == NV_RESET_WATCHDOG ? 3u : 1u, aux, sizeof(aux));

    mode_init(&g_mode, boot_mode,
              cause == NV_RESET_WATCHDOG ? CAUSE_WATCHDOG_RESET : CAUSE_BOOT,
              0, &mode_ops, NULL);
    if (boot_mode == MODE_SAFE) {
        g_nv.last_safe_src    = (uint8_t)CAUSE_WATCHDOG_RESET;
        g_nv.last_safe_detail = 0;
        nv_commit();
    }
    return RTEMS_SUCCESSFUL;
}
