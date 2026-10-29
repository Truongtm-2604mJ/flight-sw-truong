# Exchange logs

`host-sim-exchange.log` is written by `make demo`. It records the ground
station talking to the application built on the POSIX host simulation
(`../../host-sim`), not to RTEMS. A log from SIS should be saved next to
it as `sis-exchange.log` (see `../../README.md`).

Line format: `[ground seconds] TC -> <hex>` for telecommands sent,
`TM <- <hex>` for telemetry received, followed by the decoded packet.
`console` lines are ordinary console text from the spacecraft side.

The annotated example in `docs/tmtc-design.md` (Section 2) is the third
housekeeping report of this log. Re-running `make demo` rewrites the log
with new timestamps, so update the example if you commit a new log.
