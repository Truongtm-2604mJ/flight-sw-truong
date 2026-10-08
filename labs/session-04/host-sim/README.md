# host-sim

A POSIX implementation of the small part of the RTEMS Classic API that
the Session 4 labs call (tasks, rate monotonic periods, message queues,
uptime clock). It lets the unmodified lab sources run on a PC so the
ground scripts can be exercised without a cross toolchain.

It is a test aid only. Tasks are ordinary threads with no priorities,
so it shows nothing about scheduling or timing on the target.
