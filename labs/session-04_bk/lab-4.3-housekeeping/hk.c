/* Lab 4.3 - Housekeeping and event reporting. See hk.h. */
#include "hk.h"

#include <string.h>

static void put_i16(uint8_t *p, int16_t v)
{
    pus_put_u16(p, (uint16_t)v);
}

void hk_pack(const hk_sample_t *s, uint8_t out[HK_REPORT_LEN])
{
    out[0] = HK_SID_PLATFORM;
    pus_put_u16(&out[1], s->vbus_mv);
    put_i16(&out[3], s->t_bat_cdeg);
    put_i16(&out[5], s->t_obc_cdeg);
    out[7] = s->mode;
    pus_put_u16(&out[8], s->tc_rejected);
    pus_put_u16(&out[10], s->tc_accepted);
    pus_put_u16(&out[12], s->tm_dropped);
    pus_put_u32(&out[14], s->uptime_s);
    pus_put_u16(&out[18], s->hk_exec_us);
}

static void emit(hk_ctx_t *hk, tm_source_t *src, uint8_t service,
                 uint8_t subtype, obt_t time, const uint8_t *data, size_t len)
{
    uint8_t packet[PUS_MAX_PACKET];
    int n = pus_tm_build(src, service, subtype, 0, time, data, len,
                         packet, sizeof(packet));
    if (n > 0) {
        (void)hk->sink(packet, (size_t)n, hk->sink_arg);
    }
}

static void emit_event(hk_ctx_t *hk, uint8_t severity, uint16_t event_id,
                       const hk_sample_t *s)
{
    uint8_t aux[EVT_AUX_LEN];

    pus_put_u16(&aux[0], event_id);
    aux[2] = PARAM_ID_T_BAT;
    put_i16(&aux[3], s->t_bat_cdeg);     /* the sample that caused it */
    put_i16(&aux[5], hk->tbat_mon.limit);

    /* Stamped with the acquisition time of that sample. */
    emit(hk, &hk->event_src, PUS_SVC_EVENT, severity, s->time,
         aux, sizeof(aux));
}

void hk_cycle(hk_ctx_t *hk)
{
    hk_sample_t s;
    sensor_values_t v;
    uint8_t report[HK_REPORT_LEN];
    uint32_t enabled;

    /* 1. Acquire. The timestamp is taken here, at acquisition, and
     *    travels with the sample. It is not taken again at packetisation
     *    or at downlink. */
    s.time = hk->now();
    sensors_sim_sample(&hk->sensors, &v);
    s.vbus_mv    = v.vbus_mv;
    s.t_bat_cdeg = v.t_bat_cdeg;
    s.t_obc_cdeg = v.t_obc_cdeg;

    /* 2. Monitor. Runs every cycle whether or not reports are enabled:
     *    turning the downlink off must not blind the monitoring. Events
     *    are raised on state transitions only (edge + hysteresis). */
    switch (limit_mon_update(&hk->tbat_mon, s.t_bat_cdeg)) {
    case LIMIT_MON_WENT_HIGH:
        emit_event(hk, PUS_TM_5_LOW_SEVERITY, EVT_TEMP_BAT_HIGH, &s);
        break;
    case LIMIT_MON_WENT_NOMINAL:
        emit_event(hk, PUS_TM_5_INFORMATIVE, EVT_TEMP_BAT_NOMINAL, &s);
        break;
    case LIMIT_MON_NO_CHANGE:
    default:
        break;
    }

    /* 3. Report, if the ground has not disabled periodic generation. */
    enabled = hk->enabled;
    if (!enabled) {
        return;
    }

    s.mode = HK_MODE_ENABLED;
    if (hk->tbat_mon.state == LIMIT_MON_HIGH) {
        s.mode |= HK_MODE_TBAT_HIGH;
    }
    s.tc_rejected = (uint16_t)(hk->tc != NULL ? hk->tc->rejected : 0u);
    s.tc_accepted = (uint16_t)(hk->tc != NULL ? hk->tc->accepted : 0u);
    s.tm_dropped  = (uint16_t)(hk->tm_dropped != NULL ? hk->tm_dropped() : 0u);
    s.uptime_s    = s.time.coarse;
    s.hk_exec_us  = hk->last_exec_us;

    hk_pack(&s, report);
    emit(hk, &hk->hk_src, PUS_SVC_HK, PUS_TM_3_HK_REPORT, s.time,
         report, sizeof(report));
}

/* ---- Service 3 telecommands ------------------------------------------------ */

/* TC(3,5) and TC(3,6) application data: N (u8) followed by N structure
 * IDs (u8 each). This software has one structure, so N = 1, SID = 1. */
static pus_fail_t svc3_check(const pus_tc_t *tc, void *arg)
{
    (void)arg;
    if (tc->subtype != PUS_TC_3_ENABLE_PERIODIC &&
        tc->subtype != PUS_TC_3_DISABLE_PERIODIC) {
        return PUS_FAIL_SUBTYPE;
    }
    if (tc->app_len != 2u || tc->app_data[0] != 1u ||
        tc->app_data[1] != HK_SID_PLATFORM) {
        return PUS_FAIL_APP_DATA;
    }
    return PUS_OK;
}

static pus_fail_t svc3_exec(const pus_tc_t *tc, tc_ctx_t *ctx, void *arg)
{
    hk_ctx_t *hk = arg;

    (void)ctx;
    /* A single aligned word store: takes effect at the next HK cycle. */
    hk->enabled = (tc->subtype == PUS_TC_3_ENABLE_PERIODIC) ? 1u : 0u;
    return PUS_OK;
}

int hk_register_service(hk_ctx_t *hk, tc_ctx_t *tc)
{
    return tc_register_service(tc, PUS_SVC_HK, svc3_check, svc3_exec, hk);
}

void hk_init(hk_ctx_t *hk, tm_sink_fn sink, void *sink_arg, obt_now_fn now,
             const tc_ctx_t *tc, uint32_t (*tm_dropped)(void))
{
    memset(hk, 0, sizeof(*hk));
    hk->hk_src.apid    = APID_HK;
    hk->event_src.apid = APID_EVENT;
    hk->sink       = sink;
    hk->sink_arg   = sink_arg;
    hk->now        = now;
    hk->tc         = tc;
    hk->tm_dropped = tm_dropped;
    hk->enabled    = 1u; /* the spacecraft talks from boot */
    sensors_sim_init(&hk->sensors);
    limit_mon_init(&hk->tbat_mon, TBAT_LIMIT_CDEG, TBAT_HYSTERESIS_CDEG);
}
