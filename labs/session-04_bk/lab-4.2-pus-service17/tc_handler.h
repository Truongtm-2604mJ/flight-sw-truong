/*
 * Lab 4.2 - Telecommand handler: validation, service 1 verification
 * reporting, and dispatch by service type.
 *
 * Portable C99. The handler does no I/O of its own: telemetry leaves
 * through a sink callback and time comes from a clock callback, so the
 * same code runs under RTEMS and in the host unit tests.
 *
 * Threading: one tc_ctx_t is owned by one task (the command reception
 * task). The two counters are single writer and may be read by another
 * task (housekeeping) as plain 32 bit loads.
 */
#ifndef TC_HANDLER_H
#define TC_HANDLER_H

#include <stdint.h>
#include <stddef.h>

#include "pus.h"

#define TC_MAX_SERVICES 8u

/* PUS service 1 message subtypes. */
#define PUS_SVC_VERIFICATION       1u
#define PUS_TM_1_ACCEPT_OK         1u
#define PUS_TM_1_ACCEPT_FAIL       2u
#define PUS_TM_1_START_OK          3u
#define PUS_TM_1_COMPLETE_OK       7u
#define PUS_TM_1_COMPLETE_FAIL     8u

/* PUS service 17 message subtypes. */
#define PUS_SVC_TEST               17u
#define PUS_TC_17_ARE_YOU_ALIVE    1u
#define PUS_TM_17_ALIVE_REPORT     2u

/* Returns 0 if the packet was handed over, non zero if it was dropped. */
typedef int (*tm_sink_fn)(const uint8_t *packet, size_t len, void *arg);
typedef obt_t (*obt_now_fn)(void);

struct tc_ctx;

/*
 * A service is two functions:
 *   check  runs during acceptance. It looks at subtype and application
 *          data only and must have no side effects. It returns PUS_OK
 *          or the acceptance failure code.
 *   exec   runs after the acceptance report. It performs the command
 *          and returns PUS_OK or a completion failure code.
 * Splitting them is what guarantees "validate before you act".
 */
typedef pus_fail_t (*tc_check_fn)(const pus_tc_t *tc, void *arg);
typedef pus_fail_t (*tc_exec_fn)(const pus_tc_t *tc, struct tc_ctx *ctx,
                                 void *arg);

typedef struct {
    uint8_t     service;
    tc_check_fn check;
    tc_exec_fn  exec;
    void       *arg;
} tc_service_t;

typedef struct tc_ctx {
    tm_source_t  tm_src;       /* APID and counter for TM(1,x), TM(17,x) */
    tm_sink_fn   sink;
    void        *sink_arg;
    obt_now_fn   now;
    tc_service_t services[TC_MAX_SERVICES];
    size_t       service_count;
    volatile uint32_t accepted; /* telecommands that passed acceptance   */
    volatile uint32_t rejected; /* telecommands rejected for any reason  */
} tc_ctx_t;

/* Prepare a context. Service 17 is registered by default. */
void tc_init(tc_ctx_t *ctx, uint16_t apid, tm_sink_fn sink, void *sink_arg,
             obt_now_fn now);

/* Add a service. Returns 0, or -1 if the table is full or the service
 * type is already registered. Call before the reception task starts. */
int tc_register_service(tc_ctx_t *ctx, uint8_t service, tc_check_fn check,
                        tc_exec_fn exec, void *arg);

/* Process one received frame holding (what should be) one telecommand. */
void tc_handle(tc_ctx_t *ctx, const uint8_t *frame, size_t frame_len);

/* Count a frame that was rejected below packet level (bad link framing). */
void tc_count_link_error(tc_ctx_t *ctx);

/* Build and emit one TM packet from the handler's APID. For services. */
int tc_emit_tm(tc_ctx_t *ctx, uint8_t service, uint8_t subtype,
               uint16_t dest_id, const uint8_t *data, size_t data_len);

#endif /* TC_HANDLER_H */
