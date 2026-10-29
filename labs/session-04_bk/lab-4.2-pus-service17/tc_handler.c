/* Lab 4.2 - Telecommand handler. See tc_handler.h. */
#include "tc_handler.h"

#include <string.h>

/* ---- Service 17: test ------------------------------------------------ */

static pus_fail_t svc17_check(const pus_tc_t *tc, void *arg)
{
    (void)arg;
    if (tc->subtype != PUS_TC_17_ARE_YOU_ALIVE) {
        return PUS_FAIL_SUBTYPE;
    }
    if (tc->app_len != 0u) {
        return PUS_FAIL_APP_DATA; /* TC(17,1) carries no data */
    }
    return PUS_OK;
}

static pus_fail_t svc17_exec(const pus_tc_t *tc, tc_ctx_t *ctx, void *arg)
{
    (void)arg;
    if (tc_emit_tm(ctx, PUS_SVC_TEST, PUS_TM_17_ALIVE_REPORT,
                   tc->source_id, NULL, 0) != 0) {
        return PUS_FAIL_EXECUTION;
    }
    return PUS_OK;
}

/* ---- Service 1: request verification ---------------------------------- */

static void report_success(tc_ctx_t *ctx, const pus_tc_t *tc, uint8_t subtype)
{
    (void)tc_emit_tm(ctx, PUS_SVC_VERIFICATION, subtype, tc->source_id,
                     tc->request_id, PUS_REQUEST_ID_LEN);
}

static void report_failure(tc_ctx_t *ctx, const pus_tc_t *tc, uint8_t subtype,
                           pus_fail_t code)
{
    uint8_t data[PUS_REQUEST_ID_LEN + 1u];

    memcpy(data, tc->request_id, PUS_REQUEST_ID_LEN);
    data[PUS_REQUEST_ID_LEN] = (uint8_t)code;

    /* tc->source_id is 0 unless the packet passed every packet level
     * check, so a corrupted source ID is never used as a destination. */
    (void)tc_emit_tm(ctx, PUS_SVC_VERIFICATION, subtype, tc->source_id,
                     data, sizeof(data));
}

/* ---- Public API -------------------------------------------------------- */

void tc_init(tc_ctx_t *ctx, uint16_t apid, tm_sink_fn sink, void *sink_arg,
             obt_now_fn now)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->tm_src.apid = apid;
    ctx->sink        = sink;
    ctx->sink_arg    = sink_arg;
    ctx->now         = now;
    (void)tc_register_service(ctx, PUS_SVC_TEST, svc17_check, svc17_exec,
                              NULL);
}

int tc_register_service(tc_ctx_t *ctx, uint8_t service, tc_check_fn check,
                        tc_exec_fn exec, void *arg)
{
    size_t i;

    if (check == NULL || exec == NULL ||
        ctx->service_count >= TC_MAX_SERVICES) {
        return -1;
    }
    for (i = 0; i < ctx->service_count; i++) {
        if (ctx->services[i].service == service) {
            return -1;
        }
    }
    ctx->services[ctx->service_count].service = service;
    ctx->services[ctx->service_count].check   = check;
    ctx->services[ctx->service_count].exec    = exec;
    ctx->services[ctx->service_count].arg     = arg;
    ctx->service_count++;
    return 0;
}

int tc_emit_tm(tc_ctx_t *ctx, uint8_t service, uint8_t subtype,
               uint16_t dest_id, const uint8_t *data, size_t data_len)
{
    uint8_t packet[PUS_MAX_PACKET];
    int len;

    len = pus_tm_build(&ctx->tm_src, service, subtype, dest_id, ctx->now(),
                       data, data_len, packet, sizeof(packet));
    if (len < 0) {
        return len;
    }
    return ctx->sink(packet, (size_t)len, ctx->sink_arg);
}

void tc_count_link_error(tc_ctx_t *ctx)
{
    ctx->rejected = ctx->rejected + 1u;
}

void tc_handle(tc_ctx_t *ctx, const uint8_t *frame, size_t frame_len)
{
    pus_tc_t tc;
    pus_fail_t code;
    const tc_service_t *svc = NULL;
    size_t i;

    /* Checks 1 to 7: packet level. */
    code = pus_tc_validate(frame, frame_len, ctx->tm_src.apid, &tc);
    if (code == PUS_FAIL_NO_REPORT) {
        ctx->rejected = ctx->rejected + 1u;
        return;
    }

    /* Check 8: service implemented. */
    if (code == PUS_OK) {
        for (i = 0; i < ctx->service_count; i++) {
            if (ctx->services[i].service == tc.service) {
                svc = &ctx->services[i];
                break;
            }
        }
        if (svc == NULL) {
            code = PUS_FAIL_SERVICE;
        }
    }

    /* Checks 9 and 10: subtype and application data, by the service. */
    if (code == PUS_OK) {
        code = svc->check(&tc, svc->arg);
    }

    /* Failure reports are sent whatever the acknowledgement flags say. */
    if (code != PUS_OK) {
        ctx->rejected = ctx->rejected + 1u;
        report_failure(ctx, &tc, PUS_TM_1_ACCEPT_FAIL, code);
        return; /* never executed */
    }

    ctx->accepted = ctx->accepted + 1u;
    if (tc.ack_flags & PUS_ACK_ACCEPTANCE) {
        report_success(ctx, &tc, PUS_TM_1_ACCEPT_OK);
    }
    if (tc.ack_flags & PUS_ACK_START) {
        report_success(ctx, &tc, PUS_TM_1_START_OK);
    }

    code = svc->exec(&tc, ctx, svc->arg);

    if (code != PUS_OK) {
        report_failure(ctx, &tc, PUS_TM_1_COMPLETE_FAIL, code);
    } else if (tc.ack_flags & PUS_ACK_COMPLETION) {
        report_success(ctx, &tc, PUS_TM_1_COMPLETE_OK);
    }
}
