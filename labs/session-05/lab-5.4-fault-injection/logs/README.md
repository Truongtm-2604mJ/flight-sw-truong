# Logs

| File | What |
|---|---|
| `host-sim-modes.log` | Lab 5.3 scenario (`gs_modes.py`) against the host simulation |
| `host-sim-campaign.log` | Lab 5.4 campaign against the host simulation |
| `host-sim-check-flight.log` | Flight build integration test (`check_flight.py`): TC(200,x) rejected with code 7 |
| `host-sim-fp-1h.log` | 3600 s false positive run, host simulation, clock x10 |
| `sis-*.log` | The same runs against RTEMS on SIS (commands in `../../README.md`) |

Every telemetry packet is logged in hex and decoded on the next line.
Lines starting `# ROW` are the campaign table rows.
