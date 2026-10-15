#!/usr/bin/env python3
"""Lab 5.3 (and 5.1 requirement 5) ground scenario: mode management.

On one spacecraft run, in order:
  1. a command is put in the onboard schedule, then the ground commands
     SAFE: the schedule is suspended and the command does not fire;
  2. SAFE stays SAFE with no timeout;
  3. the ground commands NOMINAL: SAFE is left, the schedule stays
     suspended until the ground re-enables it, then the command fires;
  4. FDIR inhibit: a real limit violation is detected and reported, the
     mode change is refused and reported; inhibit visible in HK;
  5. a task hang under inhibit is detected but not restarted, and is
     restarted as soon as the inhibit is lifted;
  6. the inhibit expires on its own;
  7. monitoring disabled per parameter by TC(12,2): no event whatever the
     value; re-enabled by TC(12,1): detects; ground brings DEGRADED back;
  8. malformed FDIR commands are rejected with failure reports.

    python3 gs_modes.py --spawn ../../app/lab5-test.host
    python3 gs_modes.py --pty --spawn "sparc-rtems6-sis -leon3 -rt -r lab5-test.exe"
"""

import argparse
import os

import fdir
from campaign import events_in, is_event, is_hk
from link import Checker, Link

TAG = 0x0042            # source ID of the scheduled command, to recognise it


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    Link.add_arguments(p)
    a = p.parse_args()
    if os.path.exists(a.nv_file):
        os.remove(a.nv_file)

    link = Link(a)
    check = Checker(link)

    def command(cmd, note, expect=7):
        tc = link.send(cmd, note)
        rep = link.wait_for(lambda t: t.service == 1 and t.subtype in (2, 7, 8)
                            and t.data[:4] == tc[:4], 10)
        got = rep.subtype if rep else None
        code = rep.data[4] if rep is not None and rep.subtype == 2 else None
        check.check(got == expect, f"{note}: TM(1,{expect}) (got TM(1,{got})"
                    + (f" code {code})" if code is not None else ")"))
        return rep

    def hk_status():
        tm = link.wait_for(is_hk(), 5)
        return fdir.decode_hk(tm) if tm else None

    def mark():
        return len(link.telemetry)

    def since(m):
        # Events caused by a command's execution arrive BEFORE its
        # completion report, so look at everything since the mark.
        return link.telemetry[m:]

    def scheduled_fired(tms):
        return any(t.kind == (17, 2) and t.dest_id == TAG for t in tms)

    boot = link.wait_for(is_event(fdir.EVT_BOOT), 15)
    check.check(boot is not None and fdir.decode_event(boot)["mode"] == 0,
                "cold boot in NOMINAL")
    hk = link.wait_for(is_hk(1), 5)
    now = hk.time_coarse

    # -- 1. schedule a command, then SAFE --------------------------------------------
    link.log("# 1. Scheduled command, then ground commands SAFE")
    inner = fdir.build_tc(link.next_tc_seq(), 17, 1, source_id=TAG)
    command(fdir.tc_sched_insert(now + 4, inner),
            f"schedule TC(17,1) for onboard t={now + 4} s")
    m = mark()
    command(fdir.tc_set_mode("SAFE"), "ground: SAFE")
    link.collect(8)
    tms = since(m)
    ev = [e for _, e in events_in(tms)]
    mode = [e for e in ev if e["id"] == fdir.EVT_MODE]
    check.check(len(mode) == 1 and mode[0]["to"] == 2 and mode[0]["src"] == 1,
                "transition NOMINAL -> SAFE reported, cause GROUND")
    check.check(any(e["id"] == fdir.EVT_SCHED_SUSPENDED and e["entries"] == 1
                    for e in ev), "schedule suspended, 1 activity kept")
    check.check(not scheduled_fired(tms), "scheduled command did not fire")
    hk2 = [fdir.decode_hk(t) for t in tms if t.kind == (3, 25)]
    check.check(hk2 and all(h["sid"] == 2 for h in hk2),
                "only essential HK (SID 2) in SAFE")
    h = hk2[-1] if hk2 else None
    check.check(h is not None and h["mode"] == 2
                and not h["status"] & fdir.ST_PAYLOAD
                and not h["status"] & fdir.ST_SCHED
                and h["status"] & fdir.ST_SUN and h["status"] & fdir.ST_RX,
                "SAFE: payload off, schedule off, sun pointing, receiver on")

    # -- 2. no automatic exit --------------------------------------------------------------
    link.log("# 2. SAFE has no timeout")
    tms = link.collect(10)
    check.check(not [e for _, e in events_in(tms) if e["id"] == fdir.EVT_MODE],
                "still SAFE after 18 s, no transition")

    # -- 3. ground exit; schedule needs its own decision ------------------------------
    link.log("# 3. Ground commands NOMINAL; schedule stays off until enabled")
    m = mark()
    command(fdir.tc_set_mode("NOMINAL"), "ground: NOMINAL")
    link.collect(3)
    tms = since(m)
    mode = [e for _, e in events_in(tms) if e["id"] == fdir.EVT_MODE]
    check.check(len(mode) == 1 and mode[0]["frm"] == 2 and mode[0]["to"] == 0,
                "transition SAFE -> NOMINAL reported")
    h = fdir.decode_hk([t for t in tms if t.kind == (3, 25)][-1])
    check.check(h["sid"] == 1 and h["status"] & fdir.ST_PAYLOAD
                and not h["status"] & fdir.ST_SCHED,
                "NOMINAL: full HK, payload on, schedule still suspended")
    check.check(not scheduled_fired(tms), "scheduled command still held")
    command(fdir.tc_sched_enable(), "ground: enable schedule")
    tms = link.collect(3)
    check.check(scheduled_fired(tms), "overdue scheduled command released")

    # -- 4. inhibit: detection yes, action no -------------------------------------------
    link.log("# 4. FDIR inhibited, T_BAT goes out of limits")
    command(fdir.tc_fdir_inhibit(120), "ground: inhibit FDIR actions 120 s")
    h = hk_status()
    check.check(h is not None and h["status"] & fdir.ST_INHIBITED
                and 100 <= h["inhibit_left"] <= 120,
                f"inhibit visible in HK ({h['inhibit_left'] if h else '?'} s left)")
    command(fdir.tc_inject_param("T_BAT", 4800, 0), "inject T_BAT 48 C")
    tms = link.collect(6)
    ev = [e for _, e in events_in(tms)]
    check.check(any(e["id"] == 0x0220 and e["state"] == 2 for e in ev),
                "alarm still detected and reported")
    check.check(any(e["id"] == fdir.EVT_ACTION_INHIBITED and e["kind"] == 2
                    and e["subject"] == 2 for e in ev),
                "mode change refused, ACTION_INHIBITED reported")
    check.check(not any(e["id"] == fdir.EVT_MODE for e in ev), "no mode change")
    command(fdir.tc_inject_clear(), "clear injection")
    tms = link.collect(6)
    check.check(any(e["id"] == 0x0220 and e["state"] == 0
                    for _, e in events_in(tms)), "T_BAT back in limits: cleared")

    # -- 5. task hang under inhibit ------------------------------------------------------
    link.log("# 5. PL_LOG hangs while inhibited, then inhibit lifted")
    command(fdir.tc_inject_task_hang("PL_LOG", 1), "inject PL_LOG hang x1")
    tms = link.collect(6)
    ev = [e for _, e in events_in(tms)]
    check.check(any(e["id"] == fdir.EVT_MISSED and e["task"] == 2 for e in ev),
                "heartbeat miss detected")
    check.check(any(e["id"] == fdir.EVT_ACTION_INHIBITED and e["kind"] == 1
                    for e in ev), "restart refused, ACTION_INHIBITED reported")
    check.check(not any(e["id"] == fdir.EVT_RESTARTED for e in ev), "not restarted")
    m = mark()
    command(fdir.tc_fdir_enable(), "ground: enable FDIR actions")
    link.collect(5)
    ev = [e for _, e in events_in(since(m))]
    check.check(any(e["id"] == fdir.EVT_INHIBIT and e["on"] == 0
                    and e["reason"] == 1 for e in ev), "inhibit OFF reported")
    check.check(any(e["id"] == fdir.EVT_RESTARTED and e["task"] == 2 for e in ev),
                "silent task restarted once the inhibit is lifted")

    # -- 6. inhibit expiry ------------------------------------------------------------------
    link.log("# 6. Inhibit for 5 s expires by itself")
    command(fdir.tc_fdir_inhibit(5), "ground: inhibit FDIR actions 5 s")
    tm = link.wait_for(is_event(fdir.EVT_INHIBIT, on=0), 12)
    check.check(tm is not None and fdir.decode_event(tm)["reason"] == 2,
                "inhibit expired, reported with reason TIMEOUT")

    # -- 7. per parameter monitoring enable / disable ---------------------------------------
    link.log("# 7. T_BAT monitoring disabled, then enabled again")
    command(fdir.tc_monitor(False, "T_BAT"), "ground: disable T_BAT monitoring")
    h = hk_status()
    h = hk_status()
    check.check(h is not None and h["mon_enabled"] == 0b101,
                "HK shows T_BAT monitoring disabled")
    command(fdir.tc_inject_param("T_BAT", 4800, 0), "inject T_BAT 48 C")
    tms = link.collect(6)
    check.check(not [e for _, e in events_in(tms) if e["id"] in fdir.EVT_MON],
                "disabled: no event at 48 C")
    command(fdir.tc_monitor(True, "T_BAT"), "ground: enable T_BAT monitoring")
    tm = link.wait_for(is_event(fdir.EVT_MODE, to=1), 8)
    check.check(tm is not None and fdir.decode_event(tm)["src"] == 2,
                "enabled: detected, NOMINAL -> DEGRADED, cause MONITOR")
    command(fdir.tc_inject_clear(), "clear injection")
    tms = link.collect(8)
    check.check(not [e for _, e in events_in(tms) if e["id"] == fdir.EVT_MODE],
                "DEGRADED does not return to NOMINAL by itself")
    m = mark()
    command(fdir.tc_set_mode("NOMINAL"), "ground: NOMINAL")
    link.collect(1)
    check.check(any(e["id"] == fdir.EVT_MODE and e["frm"] == 1 and e["to"] == 0
                    for _, e in events_in(since(m))),
                "DEGRADED -> NOMINAL by ground command")

    # -- 8. malformed commands ---------------------------------------------------------------
    link.log("# 8. Malformed FDIR commands")
    command((19, 9, b"\x00\x00"), "inhibit for 0 s", expect=2)
    command((19, 9, b"\x0E\x11"), "inhibit for 3601 s", expect=2)
    command((8, 1, b"\x01\x03"), "set mode 3", expect=2)
    command((12, 2, b"\x01\x00\x09"), "disable unknown parameter 9", expect=2)
    command((11, 4, b"\x01\x00\x00\x00\x10"), "schedule an empty command", expect=2)

    check.check_sequences()
    check.check(not link.errors, "every TM line passed the ground CRC check")
    check.finish()


if __name__ == "__main__":
    main()
