# Phase 6: publication texts

Replace `VIDEO_LINK` and `SITE_URL` once the video is uploaded and the page is online.

## LinkedIn headline

> Embedded and flight software | C, RTEMS, CCSDS/PUS, FDIR | Building spacecraft software that can be verified

## LinkedIn post

> For the last four months I have been building the flight software of a small Earth observation
> satellite, LOTUS-1, as the capstone of the Spacecraft Software Engineer Program. It runs on RTEMS 6 on
> a LEON3 processor (simulated with SIS) and is operated from Yamcs.
>
> In the 4-minute demo, the satellite:
>
> - rejects a forged command without replying to it;
> - detects a battery over-temperature with one event, not a flood;
> - tries three bounded recoveries;
> - goes to SAFE;
> - waits for the operators to bring it back with an authenticated command.
>
> All of it runs from the mission control screen.
>
> One thing I learned: integration finds the bugs that unit tests cannot. My thermal recovery had
> passing unit tests. Integrated, it started a second recovery sequence. "Recovery exhausted, go to
> SAFE" was requested, but the mode machine only applies requests every 2 s. In that gap the next
> housekeeping cycle saw "degraded, alarm still on" and began again. Each part was correct; together they
> broke the bound I had promised. The fix was one latch. Finding it took a test written against the
> integrated timing, not the module.
>
> What is in the repository:
>
> - 30 requirements, traced to code and tests at four levels;
> - coverage with a written justification for every uncovered line;
> - static analysis with a disposition for every finding;
> - a non-conformance log where the open items stay open;
> - a technical report with a section on what this is *not*: simulated hardware, no radiation effects, no
>   redundancy, and an authentication scheme nobody has reviewed independently.
>
> Repository: https://github.com/Truongtm-2604mJ/flight-sw-truong
> Demo: VIDEO_LINK
>
> #flightsoftware #embedded #RTEMS #spacecraft #CCSDS

## CV entry

**LOTUS-1 flight software**: RTEMS 6 / LEON3 (SIS), C99, Python, Yamcs. 2026.
github.com/Truongtm-2604mJ/flight-sw-truong

- Integrated a 7-task fixed-priority flight application with CCSDS Space Packets and ECSS PUS-C
  services 1, 3, 5, 8, 11, 12, 17, 19. Response time analysis closes at U = 0.39 (bound 0.74).
- Built autonomous FDIR: limit monitors with persistence, heartbeat supervision with bounded restarts,
  a watchdog, and bounded thermal recovery. 6 fault scenarios verified end to end; detection latency
  3.0 s, SAFE in 35 s.
- Generated the NOMINAL/DEGRADED/SAFE mode manager from a YAML model, with completeness proven and a
  CI check that the code matches the model.
- Added HMAC-SHA-256 command authentication with anti-replay, key rotation and a recovery key. Forged
  commands get no reply, and the cost is 2.0 ms per command on LEON3 (estimate).
- Verification:
  - 142 unit tests, 16 integration tests, 35 validation tests through the ground interface;
  - 96.6 % line coverage, with every uncovered line justified;
  - MC/DC on the FDIR guards;
  - MISRA C:2012 static analysis with every finding dispositioned;
  - CI on GitHub Actions with a nightly SIS job.

## Personal site

A single page is in `docs/site/index.html`. Copy it, together with `docs/figures/architecture.png` and
the PDF report, to the VPS:

```
scp docs/site/index.html docs/figures/architecture.png docs/report/LOTUS-1-technical-report.pdf user@SITE:/var/www/html/
```
