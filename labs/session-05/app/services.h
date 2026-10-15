/* Session 5 application - telecommand services 8, 11, 12, 19 (and 200 in
 * test builds) on top of the Session 4 telecommand handler. */
#ifndef SERVICES_H
#define SERVICES_H

#include "tc_handler.h"

/* Returns 0, or -1 if the handler's service table is full. */
int services_register(tc_ctx_t *ctx);

#endif /* SERVICES_H */
