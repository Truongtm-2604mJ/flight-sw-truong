/*
 * Lab 8.2 - PUS service 210 (mission specific), key management.
 *
 *   TC(210,1)  LOAD_KEY        slot u8, nonce 13, wrapped key 48 (AES-128-CCM
 *                              under the ROM key encryption key, associated
 *                              data "KEY"+slot). Into a slot that is not
 *                              active. Authenticated under the ACTIVE key.
 *   TC(210,2)  ACTIVATE        slot u8. A LOADED slot becomes the active one;
 *                              the previous one is kept, LOADED, as fallback.
 *   TC(210,3)  ERASE           slot u8. Zeroise a slot that is not active.
 *   TC(210,10) RECOVERY_SET_COUNTER  slot u8, floor u32. Recovery key only.
 *   TC(210,11) RECOVERY_ACTIVATE     slot u8. Recovery key only. As
 *                              ACTIVATE, usable when the active key is lost.
 *
 * Loading and activating are separate so that a key can be loaded, its
 * key check value compared on the ground with HK SID 3, and only then
 * switched to. A key that arrived damaged never becomes the only key.
 *
 * Execution results are reported as events, queued here (telecommand path)
 * and emitted by the housekeeping task, the single writer of APID 0x022.
 */
#ifndef SEC_SERVICE_H
#define SEC_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "tc_auth.h"
#include "tc_handler.h"

#define SEC_EVT_KEY_LOADED      0x0603u  /* slot, kcv u32                     */
#define SEC_EVT_KEY_ACTIVATED   0x0604u  /* slot, previous, kcv u32, recovery  */
#define SEC_EVT_KEY_CORRUPT     0x0605u  /* slot mask, scrub errors u16        */
#define SEC_EVT_COUNTER_VOTE    0x0606u  /* mask: no majority, largest taken   */
#define SEC_EVT_RECOVERY_USED   0x0607u  /* subtype, slot, challenge u32       */
#define SEC_EVT_KEY_ERASED      0x0608u  /* slot                               */
#define SEC_EVT_COUNTER_SET     0x0609u  /* slot, old u32, new u32             */
#define SEC_EVT_KEY_LOAD_FAILED 0x060Au  /* slot: wrapped key did not verify   */
#define SEC_EVT_FACTORY_STATE   0x060Bu  /* key store NV blank or invalid      */
#define SEC_EVT_KEY_REPAIRED    0x060Cu  /* scrub corrected an upset           */

#define SEC_EVQ_DEPTH 8u
#define SEC_EV_MAX    14u

typedef struct {
    tc_auth_t *auth;
    uint8_t  ev[SEC_EVQ_DEPTH][SEC_EV_MAX];
    uint8_t  ev_len[SEC_EVQ_DEPTH];
    uint8_t  ev_sev[SEC_EVQ_DEPTH];
    unsigned ev_head;
    unsigned ev_count;
    uint32_t ev_dropped;
} sec_service_t;

void sec_service_init(sec_service_t *s, tc_auth_t *auth);
int  sec_service_register(sec_service_t *s, tc_ctx_t *ctx);

/* Queue an event (telecommand path or boot). Drops, and counts, when full. */
void sec_service_queue(sec_service_t *s, uint8_t severity, const uint8_t *aux,
                       size_t len);
/* Take the oldest queued event. Returns its length, 0 if none. */
size_t sec_service_take(sec_service_t *s, uint8_t *severity,
                        uint8_t out[SEC_EV_MAX]);

/* Exposed for the unit tests. */
pus_fail_t sec_service_check(const pus_tc_t *tc, void *arg);
pus_fail_t sec_service_exec(const pus_tc_t *tc, struct tc_ctx *ctx, void *arg);

#endif /* SEC_SERVICE_H */
