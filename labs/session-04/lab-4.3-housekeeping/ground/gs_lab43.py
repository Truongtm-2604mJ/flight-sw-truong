#!/usr/bin/env python3
"""Lab 4.3 ground station: periodic housekeeping and event reporting.

Checks, against the running spacecraft:
  1. housekeeping reports arrive at the 500 ms collection interval
  2. TC(3,6) stops them and TC(3,5) restarts them
  3. a battery temperature excursion raises one TM(5,2) when it starts
     and one TM(5,1) when it ends, although the parameter stays above
     the limit for many housekeeping cycles in between
  4. sequence counts are consecutive on every APID

    python3 gs_lab43.py --spawn ../lab4-3.host          (host simulation)
    python3 gs_lab43.py --spawn "sis -leon3 -r ../lab4-3.exe"
    python3 gs_lab43.py --tcp localhost:5555            (QEMU serial)

Every judgement is made on ONBOARD time (packet timestamps), never on
the ground clock, so the result does not depend on how fast the
simulator runs. The simulated temperature has a 30 s period; the
default observation covers one period.
"""

import argparse
import os
import statistics
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "lab-4.2-pus-service17", "ground"))

import pus                              # noqa: E402
from link import Checker, Link          # noqa: E402

SID1 = bytes([1, pus.HK_SID_PLATFORM])  # N = 1, SID = 1
HK_PERIOD_S = 0.5
LIMIT_C = 45.0
TIMEOUT = 20.0                          # wall clock patience per step


def is_hk(tm):
    return tm.kind == (3, 25)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    Link.add_arguments(parser)
    parser.add_argument("--duration", type=float, default=30.0,
                        help="onboard seconds to observe in total")
    args = parser.parse_args()

    link = Link.from_args(args)
    check = Checker(link)

    def onboard_time():
        """Ask the spacecraft what time it is: TC(17,1), read TM(17,2)."""
        link.send_tc(17, 1, note="ping, to read the onboard time")
        tm = link.wait_for(lambda t: t.kind == (17, 2), TIMEOUT)
        link.wait_for(lambda t: t.kind == (1, 7), TIMEOUT)
        return tm.time if tm else None

    def command(subtype, app_data, note):
        """Send a service 3 TC. Returns the completion report or None."""
        tc = link.send_tc(3, subtype, app_data, note=note)
        acc = link.wait_for(lambda t: t.service == 1 and
                            pus.decode_verification(t)["request_id"] == tc[:4],
                            TIMEOUT)
        if acc is None or acc.subtype != 1:
            return None, acc
        done = link.wait_for(lambda t: t.kind == (1, 7), TIMEOUT)
        return done, acc

    # -- 1. periodic generation ------------------------------------------------
    link.log("# 1. Housekeeping is generated periodically from boot")
    hk = []
    while len(hk) < 6:
        tm = link.wait_for(is_hk, TIMEOUT)
        if tm is None:
            break
        hk.append(tm)
    check.check(len(hk) == 6, f"received {len(hk)} of 6 TM(3,25) reports")
    gaps = [b.time - a.time for a, b in zip(hk, hk[1:])]
    check.check(bool(gaps) and all(abs(g - HK_PERIOD_S) < 0.05 for g in gaps),
                "onboard timestamps are 0.500 s apart (+/- 0.05): " +
                ", ".join(f"{g:.3f}" for g in gaps))
    check.check(all(pus.decode_hk(t)["sid"] == 1 and t.apid == pus.APID_HK
                    for t in hk), "reports are SID 1 on APID 0x020")
    check.check(all(pus.decode_hk(t)["uptime_s"] == t.time_coarse for t in hk),
                "UPTIME parameter equals the packet time (stamped at "
                "acquisition)")

    # -- 2. disable / enable ------------------------------------------------------
    link.log("# 2. TC(3,6) disables periodic generation")
    done, _ = command(6, SID1, "disable periodic HK, SID 1")
    check.check(done is not None, "TC(3,6) accepted and completed")
    t_off = done.time if done else 0.0

    # Stay disabled for at least 2 s of onboard time, then look at what
    # arrived. A report sampled before the command took effect is allowed.
    t_now = t_off
    for _ in range(20):
        link.collect(1.0)
        t_now = onboard_time() or t_now
        if t_now - t_off >= 2.0:
            break
    late = [t for t in link.telemetry if is_hk(t) and t.time > t_off]
    check.check(t_now - t_off >= 2.0 and not late,
                f"no housekeeping sampled during {t_now - t_off:.1f} s of "
                f"onboard time after the command ({len(late)} found)")

    link.log("# 2. TC(3,5) enables it again")
    done, _ = command(5, SID1, "enable periodic HK, SID 1")
    check.check(done is not None, "TC(3,5) accepted and completed")
    resumed = [link.wait_for(is_hk, TIMEOUT) for _ in range(3)]
    check.check(all(resumed), "housekeeping resumed: 3 more reports received")
    if all(resumed):
        check.check(abs((resumed[1].time - resumed[0].time) - HK_PERIOD_S)
                    < 0.05, "and they are 0.500 s apart again")

    link.log("# 2. A malformed TC(3,6) is rejected and changes nothing")
    done, acc = command(6, bytes([1, 9]), "unknown SID 9")
    code = pus.decode_verification(acc).get("code") if acc else None
    check.check(done is None and acc is not None and acc.subtype == 2
                and code == 9, f"TM(1,2) with code 9 APP_DATA: got code {code}")
    after = [link.wait_for(is_hk, TIMEOUT) for _ in range(2)]
    check.check(all(after), "housekeeping is still running afterwards")
    if all(after):
        h = pus.decode_hk(after[-1])
        sent_ok = sum(1 for t in link.telemetry if t.kind == (1, 1))
        check.check(h["tc_accepted"] == sent_ok and h["tc_rejected"] == 1,
                    f"HK counters agree with the ground: TC accepted "
                    f"{h['tc_accepted']} (ground saw {sent_ok}), rejected "
                    f"{h['tc_rejected']} (ground sent 1 bad)")

    # -- 3. event reporting ----------------------------------------------------------
    link.log(f"# 3. Observing until onboard time {args.duration:.0f} s "
             "for the temperature excursion")
    while True:
        tm = link.wait_for(lambda t: True, TIMEOUT)
        if tm is None or tm.time >= args.duration:
            break

    all_hk = [pus.decode_hk(t) for t in link.telemetry if is_hk(t)]
    above = [h for h in all_hk if h["t_bat_c"] > LIMIT_C]
    events = [t for t in link.telemetry if t.service == 5]
    high = [t for t in events if t.subtype == 2]
    back = [t for t in events if t.subtype == 1]

    check.check(len(high) >= 1,
                f"TM(5,2) anomaly events received: {len(high)}")
    check.check(len(above) >= 10 * max(1, len(high)),
                f"T_BAT was above {LIMIT_C:.2f} C in {len(above)} housekeeping "
                f"reports, for {len(high)} event(s): not one event per cycle")
    expected = [2, 1] * len(events)
    check.check([t.subtype for t in events] == expected[:len(events)],
                "events strictly alternate anomaly / return-to-nominal "
                "(one per crossing): " +
                " ".join(f"(5,{t.subtype})@{t.time:.1f}s" for t in events))
    check.check(len(back) >= 1,
                f"TM(5,1) return-to-nominal events received: {len(back)}")
    if high:
        e = pus.decode_event(high[0])
        check.check(e["event_id"] == 0x0101 and e["value_c"] > e["limit_c"],
                    f"event carries the sample that tripped it: "
                    f"{e['value_c']:.2f} C > {e['limit_c']:.2f} C")
    mode_high = [h for h in all_hk if h["mode"] & 0x02]
    check.check(len(mode_high) >= len(above),
                f"MODE bit 1 (monitor HIGH) set in {len(mode_high)} reports")

    # -- 4. sequence counts and link health --------------------------------------------
    link.log("# 4. Sequence counts per APID")
    for apid in (pus.APID_CMD, pus.APID_HK, pus.APID_EVENT):
        check.check_sequence(apid)
    check.check(bool(all_hk) and all_hk[-1]["tm_dropped"] == 0,
                "TM_DROPPED is 0: the TM queue never overflowed")
    check.check(not link.errors, "every TM line passed the ground CRC check")

    # -- timing data for the design note --------------------------------------------------
    exec_us = [h["hk_exec_us"] for h in all_hk[2:]]
    if exec_us:
        link.log(f"# HK_EXEC over {len(exec_us)} reports: "
                 f"min {min(exec_us)} us, median "
                 f"{statistics.median(exec_us):.0f} us, max {max(exec_us)} us "
                 f"(wall clock on the spacecraft; includes preemption)")

    check.finish()


if __name__ == "__main__":
    main()
