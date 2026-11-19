/*
 * Telecommand security in the integrated application: authentication and
 * anti-replay (tc_auth), key store and service 210 (keystore, sec_service),
 * rate limited rejection reporting (sec_report), HK SID 3.
 *
 * Three calls connect it to the rest of the software (in Session 8 these
 * were linker wraps; in the integrated tree they are plain calls):
 *
 *   security_init()      main.c, Init, after services_register() and before
 *                        the TM/TC tasks start: loads the key store,
 *                        registers service 210, sets tc_ctx_t.security.
 *   security_hk_cycle()  main.c, TM_HK task, after fdir_hk_cycle() (500 ms):
 *                        key scrub, rejection reporter, key events, SID 3.
 *                        TM_HK is the single writer of APID 0x022.
 *   security_trusted()   tmtc.c, around a telecommand released by the onboard
 *                        schedule: authenticated when its TC(11,4) was
 *                        uplinked, so it carries no trailer.
 */
#ifndef SECURITY_H
#define SECURITY_H

#include <stdint.h>

#include "tc_handler.h"

#define APID_SEC        0x022u   /* TM(3,25) SID 3, TM(5,x) security events */
#define HK_SID_SECURITY 3u
#define HK_SID3_LEN     41u

/* HK SID 3 flags */
#define SEC_FL_FACTORY    0x01u  /* key store started from factory state  */
#define SEC_FL_NV_FAIL    0x02u  /* last NV write failed                    */
#define SEC_FL_EV_DROPPED 0x04u  /* the key event queue overflowed          */
#define SEC_FL_CTR1_UNTRUSTED 0x08u /* slot 1 waits for its first contact  */
#define SEC_FL_CTR2_UNTRUSTED 0x10u

int  security_init(tc_ctx_t *ctx);
void security_hk_cycle(void);
void security_trusted(int on);

#endif /* SECURITY_H */
