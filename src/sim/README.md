# host-sim (Session 5)

A POSIX implementation of the part of the RTEMS Classic API the Session 5
application uses, so the unmodified `app/` sources can run on a PC and be
driven by the ground scripts without a cross toolchain. Extended from the
Session 4 version with task restart, semaphores, timers, receive with
timeout, interrupt disable and `rtems_fatal()`.

It is a test aid, not a model of RTEMS:

- Tasks are ordinary threads with **no priorities and no preemption
  order**. Nothing measured here is a timing result for the target.
- `rtems_task_restart()` takes effect the next time the target task
  enters a blocking call (period, delay, semaphore, queue). On RTEMS it
  is immediate. Every supervised task blocks at least once per period,
  so the difference is at most one task period.
- The timer service routine runs in its own thread. "Interrupt disable"
  is a mutex shared with that thread.
- `rtems_fatal()` prints the source and code and exits with status 3,
  which the ground scripts treat as a processor reset.
- `HOSTSIM_SPEEDUP=<n>` runs the onboard clock n times faster than real
  time (default 1), for long false positive runs.
