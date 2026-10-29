/* HOST SIMULATION ONLY. The real <rtems/confdefs.h> sizes the RTEMS
 * workspace from the CONFIGURE_* macros; the host shim has nothing to
 * configure, so this header is intentionally empty. */
#ifndef HOST_SIM_CONFDEFS_H
#define HOST_SIM_CONFDEFS_H
#define CONFIGURE_MESSAGE_BUFFERS_FOR_QUEUE(count, size) ((count) * (size))
#endif
