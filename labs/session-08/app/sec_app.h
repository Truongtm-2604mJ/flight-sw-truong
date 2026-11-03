/*
 * Session 8 application: the Session 6 flight software (Session 5 tasks,
 * generated mode manager) with command authentication and key management.
 *
 * No Session 5 or 6 source is edited. The security layer is attached at
 * three points with linker wraps (Makefile, SEC_WRAP):
 *
 *   services_register     after the Session 5 services: load the key store,
 *                         register service 210, set tc_ctx_t.security.
 *                         Runs in Init, before the TM/TC tasks start.
 *   fdir_hk_cycle         after each housekeeping cycle (500 ms, TM_HK task):
 *                         key scrub, rejection reporter, queued key events,
 *                         HK SID 3. TM_HK is the single writer of APID 0x022.
 *   tmtc_dispatch_locked  telecommands released by the onboard schedule are
 *                         marked trusted: they were authenticated when the
 *                         TC(11,4) that carried them was uplinked.
 */
#ifndef SEC_APP_H
#define SEC_APP_H

#include <stdint.h>

#define APID_SEC        0x022u   /* TM(3,25) SID 3, TM(5,x) security events */
#define HK_SID_SECURITY 3u
#define HK_SID3_LEN     41u

/* HK SID 3 flags */
#define SEC_FL_FACTORY    0x01u  /* key store started from factory state  */
#define SEC_FL_NV_FAIL    0x02u  /* last NV write failed                    */
#define SEC_FL_EV_DROPPED 0x04u  /* the key event queue overflowed          */
#define SEC_FL_CTR1_UNTRUSTED 0x08u /* slot 1 waits for its first contact  */
#define SEC_FL_CTR2_UNTRUSTED 0x10u

#endif /* SEC_APP_H */
