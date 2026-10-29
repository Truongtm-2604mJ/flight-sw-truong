#include <rtems.h>
#include <stdio.h>
#include <stdint.h>

#include "tmtc_tasks.h"

/*
 * ============================================================
 * Lab 4.2 - PUS service 17 round trip with verification
 * ============================================================
 *
 * Tasks:
 *
 * TC_RX : reads "TC:<hex>" lines from the console, validates each
 *         telecommand and dispatches it by service and subtype.
 * TM_TX : takes TM packets from the TM queue and writes them to the
 *         console as "TM:<hex>" lines.
 *
 * Ground side: ground/gs_lab42.py
 *
 * RTEMS:
 *
 * 1 tick = 10 ms
 */

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_TASKS            (1 + TMTC_TASK_COUNT)
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES   1
#define CONFIGURE_MESSAGE_BUFFER_MEMORY \
    CONFIGURE_MESSAGE_BUFFERS_FOR_QUEUE(TM_QUEUE_DEPTH, PUS_MAX_PACKET)

/* Tasks are created with a stack larger than the default (Lab 3.3). */
#define CONFIGURE_EXTRA_TASK_STACKS (TMTC_TASK_COUNT * TMTC_TASK_STACK)

#define CONFIGURE_MICROSECONDS_PER_TICK 10000

#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INIT_TASK_STACK_SIZE (16 * 1024)

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT

#include <rtems/confdefs.h>


rtems_task Init(rtems_task_argument arg)
{
    rtems_status_code sc;

    (void)arg;

    printf("\n");
    printf("============================================\n");
    printf(" Lab 4.2 - PUS service 17 with verification\n");
    printf("============================================\n");
    printf("Command APID  : 0x%03X\n", (unsigned)APID_CMD);
    printf("Link framing  : TC:<hex> in, TM:<hex> out\n");
    printf("\n");
    fflush(stdout);

    sc = tmtc_init();

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf("ERROR tmtc_init: %s\n", rtems_status_text(sc));
        rtems_shutdown_executive(1);
    }

    sc = tmtc_start_tasks();

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf("ERROR starting TM/TC tasks: %s\n", rtems_status_text(sc));
        rtems_shutdown_executive(1);
    }

    /*
     * Init has nothing left to do. From here on the console belongs to
     * TM_TX; no other task prints.
     */
    rtems_task_exit();
}
