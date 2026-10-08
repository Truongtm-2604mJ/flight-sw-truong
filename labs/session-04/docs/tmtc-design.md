# TM/TC Design Note: Space Packet, PUS Services 1/3/5/17, Housekeeping

**Scope:** Session 4, Labs 4.1 to 4.3. Target is RTEMS 6 on LEON3 (SIS), as in Session 3.

**Status of the evidence.** The packet, command and housekeeping logic is portable C, verified on the host by 903 unit-test checks. On the target (RTEMS 6, LEON3, SIS 2.30 with `-rt`) both ground scenarios pass, Lab 4.2 with 16 of 16 checks and Lab 4.3 with 23 of 23, and the timing run completed with no missed period. Numbers are SIS values, one run each, not flight-hardware timings.

---

## 1. APID allocation

| APID | Direction | Content | Owning task |
|------|-----------|---------|-------------|
| `0x010` | TC in, TM out | All telecommands; TM(1,x) verification, TM(17,2) test report | TC_RX |
| `0x020` | TM out | TM(3,25) housekeeping report | TM_HK |
| `0x021` | TM out | TM(5,x) event reports | TM_HK |
| `0x7FF` | n/a | Reserved by CCSDS for idle packets, never used | n/a |

Why this split:

- **One APID has one writer task.** The sequence count is per APID, so if two tasks emitted on the same APID the counter would need a lock, and a lock between the command task and the periodic housekeeping task is exactly the coupling Session 3 warned about. With this table every counter is touched by one task only and no mutex exists anywhere in the TM path.
- **Events are separate from housekeeping.** Events are rare and important, housekeeping is frequent and routine. Separate APIDs give separate sequence counters, so the ground can detect a lost event even while housekeeping is disabled, and can route or store the two streams differently.
- **Command responses are separate from periodic data**, so a verification report is never confused with, or counted with, the periodic stream.

## 2. Packet structures

All fields are big endian and packed with shifts and masks (no bitfields).

| Part | TC | TM |
|------|----|----|
| Primary header (6) | version 0, type 1, sec. hdr 1, APID, flags `11`, sequence count, length | same, type 0 |
| Secondary header | PUS version 2 + ack flags (1), service (1), subtype (1), source ID (2) = **5 octets** | PUS version 2 + time status (1), service (1), subtype (1), message type counter (2), destination ID (2), time (6) = **13 octets** |
| Data | application data | source data |
| Error control (2) | CRC-16, polynomial `0x1021`, initial `0xFFFF`, over every preceding octet | same |

Time is CUC: 4 octets of seconds and 2 octets of 1/65536 s, **epoch = application start** (resolution about 15 µs). It is the only time format in the system. Housekeeping and event packets are stamped when the sample is taken, and the stamp travels with the sample; nothing is re-stamped at packetisation or downlink.

Source data layouts: TM(1,1/3/7) = request ID (first 4 octets of the TC). TM(1,2/8) = request ID + failure code (1). TM(3,25) = SID (1) + V_BUS u16 mV + T_BAT i16 0.01 °C + T_OBC i16 0.01 °C + MODE u8 + TC_REJECTED u16 + TC_ACCEPTED u16 + TM_DROPPED u16 + UPTIME u32 s + HK_EXEC u16 µs = 20 octets. TM(5,x) = event ID (2) + parameter ID (1) + value i16 + limit i16. TC(3,5/6) = N (1) + SID (1).

### Annotated example: one housekeeping report (41 octets)

From `lab-4.3-housekeeping/logs/sis-exchange.log` (RTEMS on SIS):

```
08 20 C0 02 00 22 20 03 19 00 00 00 00 00 00 00 01 05 27
01 6D 1E 11 02 09 D8 01 00 00 00 00 00 00 00 00 00 01 00 63 4C 9F
```

| Octets | Hex | Field | Value |
|--------|-----|-------|-------|
| 0-1 | `08 20` | `000` version, `0` TM, `1` sec. header, APID `000 0010 0000` | APID 0x020 |
| 2-3 | `C0 02` | `11` unsegmented, 14-bit sequence count | 2 |
| 4-5 | `00 22` | packet data length = 34 | data field is **35** octets (13 + 20 + 2), total 6 + 35 = 41 |
| 6 | `20` | PUS version 2, time reference status 0 | |
| 7-8 | `03 19` | service 3, subtype 25 | housekeeping parameter report |
| 9-10 | `00 00` | message type counter | not used |
| 11-12 | `00 00` | destination ID | 0 = ground |
| 13-18 | `00 00 00 01 05 27` | time: 1 s + 1319/65536 s | 1.020 s after start |
| 19 | `01` | SID | 1 |
| 20-21 | `6D 1E` | V_BUS | 27934 mV |
| 22-23 | `11 02` | T_BAT | 4354 = 43.54 °C |
| 24-25 | `09 D8` | T_OBC | 2520 = 25.20 °C |
| 26 | `01` | MODE | HK enabled, monitor nominal |
| 27-32 | `00 00 00 00 00 00` | TC_REJECTED, TC_ACCEPTED, TM_DROPPED | 0, 0, 0 |
| 33-36 | `00 00 00 01` | UPTIME | 1 s |
| 37-38 | `00 63` | HK_EXEC | 99 µs spent in the previous cycle's TM work |
| 39-40 | `4C 9F` | CRC-16 over octets 0 to 38 | |

For comparison, the complete TC(17,1) that the ground sends is 13 octets: `18 10 C0 00 00 06 29 11 01 00 01 30 6A` (APID 0x010, sequence 0, length field 6 for a 7-octet data field, ack flags `1001`, service 17 subtype 1, source ID 1, CRC `306A`).

## 3. Command validation sequence

Checks run in this order and stop at the first failure. A failed command is counted, reported with TM(1,2) and **never executed**. Nothing inside the packet is interpreted before check 4 has passed.

| # | Check | Failure code |
|---|-------|--------------|
| 1 | Frame holds at least a primary header (6 octets) | none: no request ID exists, the frame is dropped and counted in TC_REJECTED |
| 2 | Packet version number is 0 | 1 |
| 3 | Length field equals the octets actually received (no missing and no trailing octets), frame is at least 13 and at most 120 octets | 2 |
| 4 | CRC-16 matches | 3 |
| 5 | Type is TC, secondary header flag set, sequence flags `11` | 4 |
| 6 | APID is 0x010 | 5 |
| 7 | PUS version is 2 | 6 |
| 8 | Service type is registered | 7 |
| 9 | Subtype is known to that service | 8 |
| 10 | Application data has the right size and values | 9 |

After acceptance: TM(1,1) if requested, TM(1,3) if requested, execution, then TM(1,7) if requested or TM(1,8) with code 16 if execution failed. Success reports follow the TC's acknowledgement flags; failure reports are always sent.

Two design points. Length is checked before the CRC because the CRC position is derived from the length. And each service is two functions, `check` (no side effects, runs during acceptance) and `exec` (runs after the acceptance report), which is what makes "validate before you act" a property of the structure rather than of each handler's discipline. A failure report for a packet that did not pass every packet-level check is sent to destination 0, because its source ID field cannot be trusted.

## 4. Event detection

The battery temperature is monitored every housekeeping cycle against an upper limit of 45.00 °C with a hysteresis of 2.00 °C. The monitor is a two-state machine and reports **transitions**, not levels:

- NOMINAL and value > 45.00 °C: go to HIGH, raise TM(5,2) `TEMP_BAT_HIGH` once.
- HIGH and value < 43.00 °C: go to NOMINAL, raise TM(5,1) `TEMP_BAT_NOMINAL` once.
- Anything else: no state change, no event.

Edge detection alone is not enough, because a noisy value re-crosses the limit several times while it passes through it. The hysteresis band has to be wider than the peak-to-peak noise (here 2.00 °C against 1.60 °C). The unit test feeds one 30 s cycle of the simulated temperature to three detectors:

| Detector | Events for one excursion |
|----------|-------------------------:|
| Level (event every cycle while out of limits) | 30 |
| Edge, no hysteresis | 4 |
| Edge with hysteresis (implemented) | **1** (plus 1 on return) |

Other decisions: the monitor runs whether or not housekeeping is enabled, so switching the downlink off cannot hide an anomaly; the event carries the sample that tripped it and that sample's acquisition time; the current state is also visible as MODE bit 1 in every housekeeping report, so the ground can see "still out of limits" without any repeated event. Severity is low (5,2) because this is a first, warning-level limit; a second, higher limit would map to (5,4) and is the natural input to FDIR in Session 5.

## 5. Updated timing analysis

Measured with `lab4-3-timing.exe` on SIS (30 s, `rtems_rate_monotonic_report_statistics()`), housekeeping enabled for the whole run:

| Task | T (ms) | Periods | Missed | CPU max (ms) | Wall max (ms) | U = C/T |
|------|-------:|--------:|-------:|-------------:|--------------:|--------:|
| ACS_CTRL | 100 | 300 | 0 | 18.957 | 18.966 | 0.1896 |
| TM_HK | 500 | 60 | 0 | 47.764 | 47.773 | 0.0955 |
| PL_LOG | 1000 | 30 | 0 | 95.644 | 122.482 | 0.0956 |
| **Total** | | | **0** | | | **0.3807** |

**Still schedulable:** 0.381 is far below the Liu and Layland bound of 0.7798 for n = 3. No period was missed and no TM packet was dropped.

**Cost of the new TM work.** TM_HK's 47.764 ms already includes it. To isolate it, `hk_cycle()` is timed onboard and downlinked as the HK_EXEC parameter: 99 to 100 µs in a normal cycle (sample, monitor, pack 20 octets, build the packet, bitwise CRC over 39 octets, one non-blocking queue send), 156 µs in the one cycle that also built an event packet, and 13 µs when reports are disabled and only the monitor runs. That is about 0.2 % of TM_HK's execution time and 0.0002 of utilisation. My estimate before measuring was 0.3 to 0.5 ms, three to five times too high.

**Not comparable with Lab 3.2 loop for loop.** Lab 3.2 measured 19.812 / 49.782 / 99.979 ms for the same iteration counts. Here all three are about 4 % lower, including the two tasks whose code did not change, so the difference comes from the build (Makefile with `-O2` here, `waf` there), not from the housekeeping work. This is why the TM cost is measured directly rather than taken as a difference between the two labs.

**Response-time analysis** with the measured C (critical instant): ACS_CTRL 19.0 ms, TM_HK 47.8 + 19.0 = 66.7 ms, PL_LOG 95.6 + 2·19.0 + 47.8 = 181.3 ms, all inside their periods. The observed wall times are lower (TM_HK 47.8 ms, PL_LOG 122.5 ms) because the tasks are not released together: TM_HK starts its period about 20 ms after ACS_CTRL (visible in the housekeeping timestamps, x.020 and x.520 s) and so always runs in the gap between two ACS_CTRL jobs. The analytic values are the guaranteed bounds. Margin: TM_HK could grow by about 200 ms before the utilisation bound is reached, and by about 310 ms before PL_LOG misses its deadline in the exact analysis.

TC_RX (priority 40) and TM_TX (priority 50) sit below all three periodic tasks and share no mutex with them, so they add no interference and no blocking term; they run in the roughly 62 % slack. The only shared object is the TM queue, and `rtems_message_queue_send` never blocks: when the queue is full the packet is dropped and counted in TM_DROPPED. The price is that commands are answered only when the periodic tasks are idle, about 0.2 s in the worst case. One thing this analysis does not cover: if the console driver runs in interrupt mode, UART interrupts load the CPU above every task (housekeeping is about 175 characters per second on the link).

**If it were not schedulable**, in this order: (1) keep only sampling and timestamping in TM_HK and move packing and CRC to TM_TX, which is legal because the timestamp is taken at acquisition; (2) replace the bitwise CRC with a table; (3) generate reports every Nth cycle while still monitoring every cycle; (4) shed PL_LOG, the cheapest failure. ACS_CTRL is never touched.

## 6. Compliant, and simplified

**Believed to follow the standards.** I cross-checked the header layouts and subtype numbers against three open-source PUS-C implementations (Yamcs, the `spacepackets` library, FSFW) and found no discrepancy with the session notes. I have not yet read them against the ECSS-E-ST-70-41C text itself, which remains the authority: the Space Packet primary header and the length-minus-one convention; the idle APID being reserved; per-APID sequence counts; CRC-16 packet error control; the PUS-C secondary header field order; subtypes TM(1,1/2/3/7/8), TC(17,1)/TM(17,2), TC(3,5/6)/TM(3,25), TM(5,1..4); the request ID being the first four TC header octets; acknowledgement flags controlling success reports only.

**Simplified for the lab:**

- **Link layer.** Packets travel as hex text lines (`TC:` / `TM:`) on the console UART. There are no transfer frames, no CLTU, no COP-1, no idle packets.
- **Time.** Epoch is application start, with no P-field, no service 9 and no correlation to ground time.
- **Mission-defined widths** chosen as small as possible: 8-bit failure codes, N and SID; own layout for event auxiliary data. The message type counter is always 0.
- **Service 1.** No progress reports. A wrong APID is reported as an acceptance failure rather than a routing failure.
- **Service 3.** One fixed structure, fixed 500 ms interval; no structure definition or interval commands.
- **Service 5.** No enabling or disabling of individual events.
- **Routing.** All telecommands go to one APID, including service 3.
- **Storage.** No packet store: a full TM queue drops the newest packet. The sequence count still advances, so the ground sees the gap.
- **Size.** Packets are limited to 120 octets; segmentation is rejected.
- **Sensors** are simulated. No command authentication.
