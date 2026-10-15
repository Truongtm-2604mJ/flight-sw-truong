#!/usr/bin/env python3
"""Lab 5.4 fault injection campaign.

Runs the six injections of the session table, each on a freshly started
spacecraft, then a false positive run with no injection at all, and
writes the results table (markdown) for the design note.

Every latency is measured on the ONBOARD clock: from the onboard time of
the injection telecommand's completion report TM(1,7) to the onboard time
of the detecting event. The one exception is the corrupted telecommand,
whose detection happens before any onboard timestamp exists; that row is
the ground round trip.

    host simulation (from app/):
      python3 ../lab-5.4-fault-injection/ground/campaign.py \\
          --spawn ./lab5-test.host --label host-sim
    SIS:
      python3 ../lab-5.4-fault-injection/ground/campaign.py --pty \\
          --spawn "sparc-rtems6-sis -leon3 -rt -r lab5-test.exe" \\
          --label sis --nv-volatile
"""

import argparse
import os
import time

import fdir
from fdir import EVT_MON, MODE
from link import Checker, Link

GRACE_MS = 1000                        # WDG_GRACE_MS in app/fdir_app.h


def is_event(eid, **fields):
    def pred(tm):
        if tm.service != 5:
            return False
        e = fdir.decode_event(tm)
        return e["id"] == eid and all(e.get(k) == v for k, v in fields.items())
    return pred


def is_hk(sid=None):
    def pred(tm):
        return tm.kind == (3, 25) and (sid is None or tm.data[0] == sid)
    return pred


def events_in(tms):
    return [(tm, fdir.decode_event(tm)) for tm in tms if tm.service == 5]


class Campaign:
    def __init__(self, link, check, args):
        self.link, self.check, self.args = link, check, args
        self.rows = []
        self.fresh = True        # the Link has just started a spacecraft

    # -- helpers ---------------------------------------------------------------

    def boot(self, keep_nv=False):
        if self.fresh:
            self.fresh = False
        else:
            self.link.restart(keep_nv=keep_nv)
        self.mark = len(self.link.telemetry)
        boot = self.link.wait_for(is_event(fdir.EVT_BOOT), 15)
        self.link.wait_for(is_hk(), 15)
        return fdir.decode_event(boot) if boot else None

    def since_mark(self):
        return self.link.telemetry[self.mark:]

    def command(self, cmd, note):
        """Send a TC and wait for its completion report. Returns it."""
        tc = self.link.send(cmd, note)
        done = self.link.wait_for(
            lambda t: t.service == 1 and t.subtype in (2, 7, 8)
            and t.data[:4] == tc[:4], 10)
        ok = done is not None and done.subtype == 7
        self.check.check(ok, f"{note}: accepted and completed")
        return done

    def last_hk1(self):
        for tm in reversed(self.link.telemetry):
            if tm.kind == (3, 25) and tm.data[0] == 1:
                return fdir.decode_hk(tm)
        return None

    def add(self, fault, expected, detected, latency, isolation, recovery):
        self.rows.append((fault, expected, detected, latency, isolation,
                          recovery))
        self.link.log(f"# ROW | {fault} | {detected} | {latency} | "
                      f"{isolation} | {recovery}")

    # -- the six rows ------------------------------------------------------------

    def row_sustained_limit(self):
        self.link.log("# Row 1: T_BAT held at 48.00 C (limit 45.00 C)")
        self.boot()
        done = self.command(fdir.tc_inject_param("T_BAT", 4800, 0),
                            "inject T_BAT = 48.00 C, sustained")
        alarm = self.link.wait_for(is_event(0x0220, state=2), 15)
        mode = self.link.wait_for(is_event(fdir.EVT_MODE, to=1), 5)
        hk = self.link.wait_for(is_hk(1), 5)
        evts = events_in(self.since_mark())
        others = [e for _, e in evts if e["id"] in EVT_MON and e["param"] != 2]
        ok_iso = alarm is not None and not others
        lat = (alarm.time - done.time) * 1000 if alarm and done else None
        h = fdir.decode_hk(hk) if hk else None
        self.check.check(alarm is not None, "alarm event raised for T_BAT")
        self.check.check(mode is not None and fdir.decode_event(mode)["src"] == 2
                         and fdir.decode_event(mode)["detail"] == 2,
                         "mode NOMINAL -> DEGRADED, cause MONITOR T_BAT")
        self.check.check(ok_iso, "no other monitor raised")
        self.check.check(h is not None and not h["status"] & fdir.ST_PAYLOAD,
                         "payload suspended in DEGRADED")
        alarms = [e for _, e in events_in(self.link.collect(5))
                  if e["id"] == 0x0220]
        self.check.check(not alarms, "no further T_BAT events while it stays high")
        self.add("Parameter above high limit, sustained (T_BAT 48 C)",
                 "Monitor alarm, event raised",
                 "yes" if alarm else "NO",
                 f"{lat:.0f} ms (alarm on the 3rd consecutive sample, "
                 f"1 Hz in NOMINAL)" if lat is not None else "-",
                 "yes (T_BAT only)" if ok_iso else "NO",
                 f"DEGRADED, payload off" if mode else "NO mode change")

    def row_single_spike(self):
        self.link.log("# Row 2: one sample of T_BAT at 48.00 C")
        self.boot()
        hk0 = self.last_hk1()
        self.command(fdir.tc_inject_param("T_BAT", 4800, 1),
                     "inject T_BAT = 48.00 C, one sample")
        tms = self.link.collect(8)
        mon = [e for _, e in events_in(tms) if e["id"] in EVT_MON]
        hk1 = self.last_hk1()
        filtered = hk1["filtered"] - hk0["filtered"] if hk0 and hk1 else -1
        self.check.check(not mon, "no monitor event")
        self.check.check(filtered == 1, f"filtered transient counter +1 "
                                        f"(got {filtered})")
        self.check.check(hk1 is not None and hk1["mode"] == 0, "still NOMINAL")
        self.add("Single out of limit spike (T_BAT 48 C, 1 sample)",
                 "No event, filtered by persistence",
                 "filtered, no event" if not mon else "NO: event raised",
                 "n/a",
                 "yes (counted on T_BAT)" if filtered == 1 else "NO",
                 "no action, NOMINAL" if hk1 and hk1["mode"] == 0 else "NO")

    def row_task_stops_once(self):
        self.link.log("# Row 3: PL_LOG stops setting its heartbeat (once)")
        self.boot()
        done = self.command(fdir.tc_inject_task_hang("PL_LOG", 1),
                            "inject PL_LOG hang x1")
        missed = self.link.wait_for(is_event(fdir.EVT_MISSED), 10)
        restarted = self.link.wait_for(is_event(fdir.EVT_RESTARTED), 5)
        after = self.link.collect(8)
        live = [e for _, e in events_in(self.since_mark())
                if 0x0301 <= e["id"] <= 0x0305]
        ok_iso = bool(live) and all(e["task"] == 2 for e in live
                                    if "task" in e)
        again = [e for _, e in events_in(after) if e["id"] == fdir.EVT_MISSED]
        hk = self.last_hk1()
        lat = (missed.time - done.time) * 1000 if missed and done else None
        self.check.check(missed is not None, "heartbeat miss detected")
        self.check.check(restarted is not None, "level 1: task restarted")
        self.check.check(ok_iso, "isolated to PL_LOG")
        self.check.check(not again, "heartbeat back after the restart")
        self.check.check(hk is not None and hk["restarts"] == [0, 0, 1]
                         and hk["mode"] == 0, "1 restart, still NOMINAL")
        self.add("Task stops setting liveness flag (PL_LOG, once)",
                 "Heartbeat miss, task restart",
                 "yes" if missed else "NO",
                 f"{lat:.0f} ms" if lat is not None else "-",
                 "yes (PL_LOG only)" if ok_iso else "NO",
                 "restarted, recovered, NOMINAL" if restarted and not again
                 else "NO")

    def row_task_fails_repeatedly(self):
        self.link.log("# Row 4: ACS_CTRL fails five times in a row")
        self.boot()
        done = self.command(fdir.tc_inject_task_hang("ACS_CTRL", 5),
                            "inject ACS_CTRL hang x5")
        first = self.link.wait_for(is_event(fdir.EVT_MISSED, task=0), 10)
        esc = self.link.wait_for(is_event(fdir.EVT_ESCALATED, task=0), 30)
        safe = self.link.wait_for(is_event(fdir.EVT_MODE, to=2), 5)
        self.link.collect(4)
        evts = [e for _, e in events_in(self.since_mark())]
        restarts = [e for e in evts if e["id"] == fdir.EVT_RESTARTED]
        ok_iso = all(e.get("task", 0) == 0 for e in evts
                     if 0x0301 <= e["id"] <= 0x0305)
        lat1 = (first.time - done.time) * 1000 if first and done else None
        late = (esc.time - done.time) * 1000 if esc and done else None
        s = fdir.decode_event(safe) if safe else None
        self.check.check(len(restarts) == 3, f"exactly 3 restarts (got {len(restarts)})")
        self.check.check(esc is not None, "escalated instead of a 4th restart")
        self.check.check(s is not None and s["src"] == 3 and s["detail"] == 0,
                         "mode -> SAFE, cause LIVENESS ACS_CTRL")
        self.check.check(ok_iso, "isolated to ACS_CTRL")
        self.add("Task fails repeatedly, five times (ACS_CTRL)",
                 "Escalation to SAFE after bounded retries",
                 "yes" if esc else "NO",
                 f"first miss {lat1:.0f} ms, escalation {late:.0f} ms"
                 if lat1 is not None and late is not None else "-",
                 "yes (ACS_CTRL only)" if ok_iso else "NO",
                 f"{len(restarts)} restarts, then SAFE" if s else "NO SAFE")

    def row_corrupted_tc(self):
        self.link.log("# Row 5: telecommand with a corrupted checksum")
        self.boot()
        good = fdir.build_tc(self.link.next_tc_seq(), 17, 1)
        bad = good[:-1] + bytes([good[-1] ^ 0x01])
        t0 = time.monotonic()
        self.link.send_raw(bad, "TC(17,1) with the last CRC bit flipped")
        rep = self.link.wait_for(lambda t: t.service == 1 and
                                 t.data[:4] == bad[:4], 5)
        rtt = (time.monotonic() - t0) * 1000
        tail = self.link.collect(1.5)
        executed = any(t.kind == (17, 2) for t in tail + ([rep] if rep else []))
        code = rep.data[4] if rep is not None and len(rep.data) > 4 else None
        self.check.check(rep is not None and rep.subtype == 2 and code == 3,
                         "TM(1,2) with failure code 3 CHECKSUM")
        self.check.check(not executed, "not executed (no TM(17,2))")
        self.add("Corrupted telecommand checksum",
                 "Rejection plus failure report, no execution",
                 "yes" if code == 3 else "NO",
                 f"{rtt:.1f} ms ground round trip (before any onboard "
                 f"timestamp exists)",
                 "yes (code 3 CHECKSUM)" if code == 3 else "NO",
                 "rejected, not executed" if not executed else "NO: executed")

    def row_supervisor_hang(self):
        self.link.log("# Row 6: the supervisor task hangs")
        self.boot()
        done = self.command(fdir.tc_inject_supervisor_hang(),
                            "inject supervisor hang")
        warn = self.link.wait_for(is_event(fdir.EVT_WDG), 15)
        w_wall = time.monotonic()
        reset = self.link.wait_exit(10)
        r_wall = time.monotonic()
        lat = (warn.time - done.time) * 1000 if warn and done else None
        self.check.check(warn is not None, "watchdog early warning")
        self.check.check(reset, "processor reset after the warning")

        boot = self.boot(keep_nv=True)
        hk2 = self.link.wait_for(is_hk(2), 10)
        h = fdir.decode_hk(hk2) if hk2 else None
        survived = boot is not None and boot["last_reset"] == 1
        if survived:
            self.check.check(boot["reset_count"] == 1, "reset counted in NV")
            self.check.check(boot["mode"] == 2 and h is not None
                             and h["mode"] == 2 and h["cause_src"] == 4,
                             "booted into SAFE, cause WATCHDOG_RESET")
            recovery = "reset, boot in SAFE, cause read back from NV"
        elif self.args.nv_volatile:
            self.check.check(boot is not None and boot["nv_valid"] == 0,
                             "cold boot: NV not preserved by this target")
            recovery = ("reset; cause recorded before reset, NOT preserved "
                        "(no persistent memory between SIS runs)")
        else:
            self.check.check(False, "reset cause read back from NV")
            recovery = "NO: cause lost"
        self.add("Supervisor task hangs",
                 "Watchdog reset, cause recorded",
                 "yes" if warn and reset else "NO",
                 f"early warning {lat:.0f} ms; reset seen by the ground "
                 f"{1000 * (r_wall - w_wall):.0f} ms after the warning event "
                 f"(grace {GRACE_MS} ms from the onboard warning)"
                 if lat is not None else "-",
                 "yes (watchdog, not a task)" if warn else "NO",
                 recovery)

    # -- false positive run -----------------------------------------------------------

    def false_positive_run(self, seconds):
        self.link.log(f"# False positive run: {seconds} s of onboard time, "
                      "no injection")
        self.boot()
        start = self.link.telemetry[-1].time
        end_time = start
        deadline = time.monotonic() + seconds * 3 + 60
        while time.monotonic() < deadline:
            tm = self.link.poll(5)
            if tm is not None:
                end_time = tm.time
                if tm.time - start >= seconds:
                    break
        evts = [e for _, e in events_in(self.since_mark())
                if e["id"] != fdir.EVT_BOOT]
        hk = self.last_hk1()
        samples = sum(1 for t in self.since_mark() if t.kind == (3, 25))
        expected_glitches = samples // 137 + samples // 211
        observed = end_time - start
        self.check.check(observed >= seconds * 0.99,
                         f"observed {observed:.0f} s of onboard time")
        self.check.check(not evts, f"zero detections ({len(evts)} events)")
        self.check.check(hk is not None and hk["mode"] == 0
                         and sum(hk["restarts"]) == 0,
                         "still NOMINAL, no restart")
        self.check.check(hk is not None and hk["filtered"] >= expected_glitches,
                         f"{hk['filtered'] if hk else '?'} natural transients "
                         f"filtered (at least {expected_glitches} expected)")
        return dict(seconds=observed, samples=samples, events=len(evts),
                    filtered=hk["filtered"] if hk else None,
                    expected=expected_glitches,
                    mode=MODE[hk["mode"]] if hk else "?")

    # -- report --------------------------------------------------------------------------

    def write(self, path, fp):
        a = self.args
        lines = [
            f"# Fault injection campaign: {a.label}",
            "",
            f"Target: `{a.spawn or a.tcp}`. Generated by "
            "`lab-5.4-fault-injection/ground/campaign.py`; each row runs on "
            "a freshly started spacecraft.",
            "",
            "| Injected fault | Expected detection | Detected | Latency | "
            "Correct isolation | Recovery result |",
            "|---|---|---|---|---|---|",
        ]
        for r in self.rows:
            lines.append("| " + " | ".join(r) + " |")
        if fp:
            lines += [
                "",
                "## False positive run",
                "",
                f"{fp['seconds']:.0f} s of onboard time with no injection, "
                f"{fp['samples']} housekeeping reports: **{fp['events']} "
                f"detections**, final mode {fp['mode']}, "
                f"{fp['filtered']} natural single sample glitches rejected by "
                f"persistence (the sensor model produces at least "
                f"{fp['expected']} in that time).",
            ]
        lines += ["", f"Checks: {self.check.passed} passed, "
                      f"{self.check.failed} failed.", ""]
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        with open(path, "w") as f:
            f.write("\n".join(lines))
        self.link.log(f"# results written to {path}")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    Link.add_arguments(p)
    p.add_argument("--label", default="host-sim")
    p.add_argument("--results", help="markdown output "
                   "(default ../results/campaign-<label>.md)")
    p.add_argument("--fp-seconds", type=float, default=300)
    p.add_argument("--rows", default="1,2,3,4,5,6")
    p.add_argument("--nv-volatile", action="store_true",
                   help="the target loses NV memory across a restart (SIS)")
    a = p.parse_args()
    if a.results is None:
        a.results = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                 "..", "results", f"campaign-{a.label}.md")
    if os.path.exists(a.nv_file):
        os.remove(a.nv_file)

    link = Link(a)
    check = Checker(link)
    c = Campaign(link, check, a)
    rows = {"1": c.row_sustained_limit, "2": c.row_single_spike,
            "3": c.row_task_stops_once, "4": c.row_task_fails_repeatedly,
            "5": c.row_corrupted_tc, "6": c.row_supervisor_hang}
    for r in a.rows.split(","):
        if r.strip():
            rows[r.strip()]()
    fp = c.false_positive_run(a.fp_seconds) if a.fp_seconds > 0 else None
    check.check(not link.errors, "every TM line passed the ground CRC check")
    c.write(a.results, fp)
    check.finish()


if __name__ == "__main__":
    main()
