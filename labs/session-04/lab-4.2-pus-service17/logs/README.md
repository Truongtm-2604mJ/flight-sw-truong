# Exchange logs

`host-sim-exchange.log` is written by `make demo`. It records the ground
station talking to the application built on the POSIX host simulation
(`../../host-sim`), not to RTEMS.

`sis-exchange.log` is the same scenario against RTEMS 6 / LEON3 on SIS
(commands in `../../README.md`).

Line format: `[ground seconds] TC -> <hex>` for telecommands sent,
`TM <- <hex>` for telemetry received, followed by the decoded packet.
`console` lines are ordinary console text from the spacecraft side.
