#!/usr/bin/env python3
"""Lab 7.4 - Validation campaign.

Drives the complete flight software only through its ground interface (TC
lines in, TM lines out, the Session 5 ground link), so every test sees the
system as the operator sees it. Each validation test case (VT-...) states
its requirements, preconditions, steps and expected result; the script
records the actual result and a verdict.

    # host simulation (tests/Makefile: make validation)
    python3 run_campaign.py --target host-sim --spawn ../../labs/session-06/app/lab6-test.host \
        --spawn-flight ../../labs/session-06/app/lab6-flight.host \
        --out ../../labs/session-07/validation/results-host-sim

    # RTEMS 6 / LEON3 on SIS (make target, nightly CI)
    python3 run_campaign.py --target sis --pty --nv-volatile \
        --spawn "sparc-rtems6-sis -leon3 -rt -r lab6-test.exe" \
        --spawn-flight "sparc-rtems6-sis -leon3 -rt -r lab6-flight.exe" --out .../results-sis

    python3 run_campaign.py --spec-only --out ...   # write test-spec.md only

Outputs in --out: results.md, results.json, campaign.log. The test
specification (labs/session-07/validation/test-spec.md) is written from the
same definitions, so the specification and the executed tests cannot drift.
Exit code: 0 if no test FAILED (N/A and OBSERVED do not fail the run).
"""

import argparse
import copy
import datetime
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
GROUND = os.path.join(ROOT, "labs", "session-05", "lab-5.4-fault-injection", "ground")
sys.path.insert(0, GROUND)

import fdir  # noqa: E402  (Session 5 ground codec)
from link import Link  # noqa: E402

EVT_DISCARD = 0x0404                    # model v3, MM-140 (fdir6.c)
EV_TC_MODE_NOMINAL = 0                  # mode_sm_event_t order in mode_sm.h
TAG = 0x0042                            # source ID of the scheduled command
MODE = fdir.MODE
NOMINAL, DEGRADED, SAFE = 0, 1, 2
CAUSE_GROUND, CAUSE_MONITOR, CAUSE_LIVENESS, CAUSE_WDG = 1, 2, 3, 4
REASON_TIMEOUT = 2

TESTS = []


def vt(tid, reqs, title, run, pre, steps, expected):
    def deco(fn):
        TESTS.append(dict(id=tid, reqs=reqs, title=title, run=run, pre=pre,
                          steps=steps, expected=expected, fn=fn))
        return fn
    return deco


# ---- Campaign engine ------------------------------------------------------------------


class Campaign:
    def __init__(self, a):
        self.a = a
        self.link = None
        self.segments = []          # (start index, label) per boot, for TMTC-050
        self.results = {}
        self.ctx = {}               # values handed from one test to the next

    # -- spacecraft runs --------------------------------------------------------
    def boot(self, label, flight=False, keep_nv=False):
        if flight or self.link is None or getattr(self, "_flight", False) != flight:
            if self.link is not None:
                self.link.stop()
            args = copy.copy(self.a)
            args.spawn = self.a.spawn_flight if flight else self.a.spawn
            if not keep_nv and os.path.exists(args.nv_file):
                os.remove(args.nv_file)
            if self.link is None:
                self.link = Link(args)
            else:
                self.link.args = args
                self.link.start()
            self._flight = flight
        else:
            self.link.restart(keep_nv=keep_nv)
        self.link.log(f"# ---- spacecraft run: {label}")
        self.segments.append((len(self.link.telemetry), label))
        self.mark = len(self.link.telemetry)
        boot = self.link.wait_for(self.is_event(fdir.EVT_BOOT), 20)
        self.link.wait_for(self.is_hk(), 10)
        return fdir.decode_event(boot) if boot else None

    # -- predicates and helpers ---------------------------------------------------
    @staticmethod
    def is_event(eid, **fields):
        def pred(tm):
            if tm.service != 5 or len(tm.data) < 2:
                return False
            e = fdir.decode_event(tm)
            return e["id"] == eid and all(e.get(k) == v for k, v in fields.items())
        return pred

    @staticmethod
    def is_hk(sid=None):
        return lambda tm: tm.kind == (3, 25) and (sid is None or tm.data[0] == sid)

    def since(self, idx):
        return self.link.telemetry[idx:]

    def now_idx(self):
        return len(self.link.telemetry)

    def events(self, idx, eid=None):
        out = []
        for tm in self.since(idx):
            if tm.service == 5 and len(tm.data) >= 2:
                e = fdir.decode_event(tm)
                if eid is None or e["id"] == eid:
                    out.append((tm, e))
        return out

    def last_hk(self, sid=None):
        for tm in reversed(self.link.telemetry):
            if tm.kind == (3, 25) and (sid is None or tm.data[0] == sid):
                return tm, fdir.decode_hk(tm)
        return None, None

    def onboard_now(self):
        return self.link.telemetry[-1].time if self.link.telemetry else 0.0

    def send(self, cmd, note, **kw):
        """Send a TC; return (packet, final report or None, acceptance report)."""
        tc = self.link.send(cmd, note, **kw)
        idx = self.now_idx()
        final = self.link.wait_for(lambda t: t.service == 1 and t.subtype in (2, 7, 8)
                                   and t.data[:4] == tc[:4], 10)
        acc = next((t for t in self.since(idx) if t.kind == (1, 1) and t.data[:4] == tc[:4]),
                   None)
        return tc, final, acc

    def command(self, cmd, note, **kw):
        tc, final, _ = self.send(cmd, note, **kw)
        return final if final is not None and final.subtype == 7 else None

    def write_raw(self, text, note):
        self.link.log(f"TC -> {text}  {note}")
        self.link._write(text.encode() + b"\n")

    def wait(self, seconds):
        self.link.collect(seconds)

    def mode(self):
        _, h = self.last_hk()
        return h["mode"] if h else None


# ---- Validation test cases --------------------------------------------------------------
# Runs: each run starts a fresh spacecraft (blank NV unless stated).

# R1 - TM/TC basics, fresh test build ------------------------------------------------------

@vt("VT-01", ["MM-090"], "Cold boot comes up in NOMINAL with no transition report", "R1",
    "Blank NV memory.",
    ["Start the spacecraft.", "Read the boot report TM(5,1) and the first 3 s of telemetry."],
    "Boot report: reset cause none, NV blank, mode NOMINAL. HK mode NOMINAL. No mode transition event.")
def vt_cold_boot(c):
    boot = c.boot("R1 TM/TC basics")
    c.wait(3)
    trans = c.events(c.mark, fdir.EVT_MODE)
    ok = (boot is not None and boot["mode"] == NOMINAL and boot["last_reset"] == 0
          and boot["nv_valid"] == 0 and c.mode() == NOMINAL and not trans)
    return ok, (f"boot report mode {MODE.get(boot['mode']) if boot else '-'}, last reset "
                f"{boot['last_reset'] if boot else '-'}, NV valid {boot['nv_valid'] if boot else '-'}; "
                f"HK mode {MODE.get(c.mode())}; {len(trans)} transition events")


@vt("VT-TMTC-010a", ["TMTC-010"], "Acknowledged TC(17,1) in NOMINAL; reports follow the flags", "R1",
    "Spacecraft in NOMINAL.",
    ["Send TC(17,1) with acknowledgement flags 1001 (acceptance, completion).",
     "Send TC(17,1) with flags 0000."],
    "First: TM(1,1), TM(17,2), TM(1,7) in that order, each verification report carrying the TC's "
    "request ID and addressed to its source ID. Second: TM(17,2) only.")
def vt_ping(c):
    idx = c.now_idx()
    tc, final, acc = c.send((17, 1, b""), "are you alive (ack 1001)", ack=0b1001)
    seq = [t.kind for t in c.since(idx) if t.apid == fdir.APID_CMD]
    ok1 = (seq[:3] == [(1, 1), (17, 2), (1, 7)] and acc is not None and final is not None
           and acc.data[:4] == tc[:4] and final.data[:4] == tc[:4] and final.dest_id == 1)
    idx = c.now_idx()
    c.link.send((17, 1, b""), "are you alive (ack 0000)", ack=0)
    c.wait(1.5)
    seq2 = [t.kind for t in c.since(idx) if t.apid == fdir.APID_CMD]
    ok2 = seq2 == [(17, 2)]
    return ok1 and ok2, f"flags 1001: {seq[:3]}, request ID echoed {ok1}; flags 0000: {seq2}"


@vt("VT-TMTC-020", ["TMTC-020", "MM-120"], "Acceptance failures: reported with the first failed "
    "check's code, never executed", "R1",
    "Spacecraft in NOMINAL. Read the HK TC rejected counter.",
    ["Send TC(17,1) with a corrupted CRC.", "Send TC(17,1) to APID 0x055.",
     "Send TC(99,1).", "Send TC(17,9).", "Send TC(8,1) with mode value 3.",
     "Send TC(19,9) with a duration of 0 s.", "Wait 3 s; read HK."],
    "TM(1,2) codes 3, 5, 7, 8, 9, 9 in that order; no TM(1,7) and no TM(17,2) for any of them; "
    "mode still NOMINAL; HK TC rejected counter up by 6.")
def vt_rejections(c):
    _, h0 = c.last_hk(1)
    codes, executed = [], 0
    cases = [
        ("bad CRC", None),
        ("wrong APID", dict(apid=0x055)),
        ("unknown service", (99, 1, b"")),
        ("unknown subtype", (17, 9, b"")),
        ("mode value 3", (8, 1, struct.pack(">BB", 1, 3))),
        ("inhibit 0 s", (19, 9, struct.pack(">H", 0))),
    ]
    for note, spec in cases:
        idx = c.now_idx()
        if note == "bad CRC":
            pkt = bytearray(fdir.build_tc(c.link.next_tc_seq(), 17, 1))
            pkt[-1] ^= 1
            c.link.send_raw(bytes(pkt), "TC(17,1) corrupted CRC")
            rid = bytes(pkt[:4])
        elif note == "wrong APID":
            pkt = fdir.build_tc(c.link.next_tc_seq(), 17, 1, apid=0x055)
            c.link.send_raw(pkt, "TC(17,1) to APID 0x055")
            rid = pkt[:4]
        else:
            rid = c.link.send(spec, note)[:4]
        rep = c.link.wait_for(lambda t, r=rid: t.service == 1 and t.subtype in (2, 7, 8)
                              and t.data[:4] == r, 5)
        codes.append(rep.data[4] if rep is not None and rep.subtype == 2 else
                     (f"TM(1,{rep.subtype})" if rep else None))
        executed += sum(1 for t in c.since(idx) if t.kind in ((1, 7), (17, 2)))
    c.wait(3)
    _, h1 = c.last_hk(1)
    rej = (h1["tc_rej"] - h0["tc_rej"]) if h0 and h1 else None
    ok = codes == [3, 5, 7, 8, 9, 9] and executed == 0 and c.mode() == NOMINAL and rej == 6
    return ok, (f"codes {codes}; {executed} executions; mode {MODE.get(c.mode())}; "
                f"TC rejected counter +{rej}")


@vt("VT-TMTC-030", ["TMTC-030"], "Undecodable lines are discarded and counted", "R1",
    "Spacecraft in NOMINAL. Read the HK TC rejected counter.",
    ["Send the lines `TC:181` (odd digits), `TC:ZZ10` (not hex), a line of 121 octets, "
     "and `TC:1810` (shorter than a primary header).",
     "Wait 3 s; read HK; send TC(17,1)."],
    "No TM(1,x) for any of the four lines; HK TC rejected counter up by 4; TC(17,1) still answered.")
def vt_link_noise(c):
    _, h0 = c.last_hk(1)
    idx = c.now_idx()
    for text, note in (("TC:181", "odd number of digits"), ("TC:ZZ10", "not hex"),
                       ("TC:" + "18" * 121, "121 octets"), ("TC:1810", "2 octets")):
        c.write_raw(text, note)
    c.wait(3)
    replies = [t for t in c.since(idx) if t.service == 1]
    _, h1 = c.last_hk(1)
    rej = (h1["tc_rej"] - h0["tc_rej"]) if h0 and h1 else None
    alive = c.command((17, 1, b""), "still alive") is not None
    return (not replies and rej == 4 and alive,
            f"{len(replies)} verification reports; TC rejected counter +{rej}; alive {alive}")


@vt("VT-TMTC-060", ["TMTC-060"], "Full housekeeping every 1.0 s in NOMINAL", "R1",
    "Spacecraft in NOMINAL.",
    ["Record HK SID 1 reports for 12 s.", "Compute the onboard time between consecutive reports."],
    "At least 11 intervals, every one within 0.9 s to 1.1 s.")
def vt_hk_rate(c):
    idx = c.now_idx()
    c.wait(12)
    t = [tm.time for tm in c.since(idx) if tm.kind == (3, 25) and tm.data[0] == 1]
    d = [b - a for a, b in zip(t, t[1:])]
    ok = len(d) >= 11 and all(0.9 <= x <= 1.1 for x in d)
    return ok, (f"{len(d)} intervals, min {min(d):.3f} s, max {max(d):.3f} s" if d
                else "no HK")


# R2 - ground mode commands and SAFE ------------------------------------------------------

@vt("VT-TMTC-040", ["TMTC-040"],
    "Ground SAFE from NOMINAL: completion report, effect, report and latency", "R2",
    "Fresh spacecraft in NOMINAL. A TC(17,1) with source ID 0x0042 scheduled 6 s ahead "
    "(prepared for VT-MM-020).",
    ["Send TC(8,1) SAFE.",
     "Note the onboard time of its TM(1,7) and of the mode transition report TM(5,x).",
     "Read the first HK report generated after TM(1,7)."],
    "The mode transition is reported no later than TM(1,7), and the first HK after TM(1,7) shows "
    "SAFE. (The same execution provides the data for VT-MM-150 and VT-MM-080.)")
def vt_ground_safe(c):
    c.boot("R2 ground mode commands")
    release = int(c.onboard_now()) + 6
    inner = fdir.build_tc(c.link.next_tc_seq(), 17, 1, source_id=TAG)
    c.ctx["sched_ok"] = c.command(fdir.tc_sched_insert(release, inner),
                                  f"schedule TC(17,1) at onboard {release} s") is not None
    c.ctx["release"] = release
    idx = c.now_idx()
    done = c.command(fdir.tc_set_mode("SAFE"), "ground: SAFE")
    ev = c.link.wait_for(c.is_event(fdir.EVT_MODE, to=SAFE), 6)
    c.wait(2.5)
    e = fdir.decode_event(ev) if ev else None
    t7 = done.time if done else None
    tm = ev.time if ev else None
    hk_after = next((h for h in c.since(idx) if h.kind == (3, 25) and t7 is not None
                     and h.time > t7), None)
    hk_mode = fdir.decode_hk(hk_after)["mode"] if hk_after else None
    lat = (tm - t7) if (tm is not None and t7 is not None) else None
    c.ctx.update(lat=lat, ev=e, hk_mode_after_tm17=hk_mode, t7=t7, tmode=tm)
    tmtc040 = lat is not None and lat <= 0.0 and hk_mode == SAFE
    return tmtc040, (f"TM(1,7) at {t7:.3f} s, transition report at {tm:.3f} s "
                     f"({lat:+.3f} s after TM(1,7)); first HK after TM(1,7): "
                     f"{MODE.get(hk_mode)}" if lat is not None else "no transition report")


@vt("VT-MM-150", ["MM-150"], "Ground mode command latency", "R2",
    "As VT-TMTC-040 (same execution).",
    ["Use the times recorded in VT-TMTC-040."],
    "Transition reported at most 3.0 s after TM(1,7).")
def vt_latency(c):
    lat = c.ctx.get("lat")
    return lat is not None and lat <= 3.0, (f"{lat:.3f} s" if lat is not None else "not measured")


@vt("VT-MM-080", ["MM-080", "MM-070"], "Transition report contents", "R2",
    "As VT-TMTC-040 (same execution).",
    ["Decode the transition report recorded in VT-TMTC-040."],
    "From NOMINAL to SAFE, cause GROUND.")
def vt_report(c):
    e = c.ctx.get("ev")
    ok = e is not None and e["frm"] == NOMINAL and e["to"] == SAFE and e["src"] == CAUSE_GROUND
    return ok, (f"{MODE.get(e['frm'])} -> {MODE.get(e['to'])}, cause "
                f"{fdir.CAUSE.get(e['src'], e['src'])}" if e else "no report")


@vt("VT-MM-030", ["MM-030"], "SAFE configuration as seen from the ground", "R2",
    "Vehicle in SAFE (VT-TMTC-040).",
    ["Record telemetry for 6 s after the transition report."],
    "Only essential HK (SID 2) after the transition; status: payload off, schedule disabled, sun "
    "pointing, receiver on; mode SAFE.")
def vt_safe_config(c):
    tmode = c.ctx.get("tmode")
    idx = c.now_idx()
    c.wait(6)
    # This boot only (onboard time restarts at every boot). Run 1 used every
    # boot's telemetry here and failed on SID 1 reports from run R1: test
    # anomaly TA-01, corrected.
    hks = [t for t in c.since(c.mark) if t.kind == (3, 25) and tmode is not None
           and t.time > tmode]
    sids = sorted({t.data[0] for t in hks})
    _, h = c.last_hk(2)
    st = h["status"] if h else 0
    ok = (sids == [2] and h is not None and h["mode"] == SAFE and not st & fdir.ST_PAYLOAD
          and not st & fdir.ST_SCHED and st & fdir.ST_SUN and st & fdir.ST_RX)
    return ok, (f"HK SIDs after the transition {sids}; status 0x{st:02X} (payload "
                f"{bool(st & fdir.ST_PAYLOAD)}, schedule {bool(st & fdir.ST_SCHED)}, sun "
                f"{bool(st & fdir.ST_SUN)}, RX {bool(st & fdir.ST_RX)}); mode "
                f"{MODE.get(h['mode']) if h else '-'}")


@vt("VT-TMTC-010b", ["TMTC-010"], "Commands are answered in SAFE", "R2",
    "Vehicle in SAFE.", ["Send TC(17,1) with flags 1001."],
    "TM(1,1), TM(17,2), TM(1,7).")
def vt_ping_safe(c):
    idx = c.now_idx()
    c.command((17, 1, b""), "are you alive, in SAFE")
    seq = [t.kind for t in c.since(idx) if t.apid == fdir.APID_CMD]
    return seq[:3] == [(1, 1), (17, 2), (1, 7)], f"{seq[:3]}"


@vt("VT-MM-020a", ["MM-020"], "Scheduled command does not fire in SAFE", "R2",
    "Vehicle in SAFE; TC(17,1) tagged 0x0042 scheduled before SAFE was entered.",
    ["Wait until 3 s after the scheduled release time."],
    "No TM(17,2) addressed to 0x0042.")
def vt_sched_safe(c):
    while c.onboard_now() < c.ctx.get("release", 0) + 3:
        c.wait(1)
    fired = any(t.kind == (17, 2) and t.dest_id == TAG for t in c.since(c.mark))
    return c.ctx.get("sched_ok") and not fired, f"scheduled command fired: {fired}"


@vt("VT-MM-010", ["MM-010", "MM-050"], "Autonomous events never leave SAFE", "R2",
    "Vehicle in SAFE.",
    ["Inject T_BAT 48 C (sustained) for 6 s, then V_BUS 25.0 V (sustained) for 6 s, then clear.",
     "Observe 6 s more."],
    "Alarms are reported, but no mode transition; mode SAFE throughout.")
def vt_safe_sticky(c):
    idx = c.now_idx()
    c.command(fdir.tc_inject_param("T_BAT", 4800), "inject T_BAT 48 C")
    c.wait(6)
    c.command(fdir.tc_inject_param("V_BUS", 25000), "inject V_BUS 25.0 V")
    c.wait(6)
    c.command(fdir.tc_inject_clear(), "clear injection")
    c.wait(6)
    alarms = [e for _, e in c.events(idx) if e["id"] in fdir.EVT_MON]
    trans = c.events(idx, fdir.EVT_MODE)
    ok = len(alarms) >= 2 and not trans and c.mode() == SAFE
    return ok, (f"{len(alarms)} monitor events, {len(trans)} transitions, mode "
                f"{MODE.get(c.mode())}")


@vt("VT-MM-110", ["MM-110", "MM-140"], "Two conflicting ground commands in one cycle", "R2",
    "Vehicle in SAFE.",
    ["Send TC(8,1) DEGRADED and TC(8,1) NOMINAL back to back (a few ms apart, so in the same 2 s "
     "cycle).", "Observe 4 s."],
    "Exactly one transition, SAFE to DEGRADED (priority 1 in SAFE); a discard report (event 0x0404) "
    "naming tc_mode_nominal; mode DEGRADED. If the two commands fall in different cycles (seen as two "
    "transitions), the vehicle is returned to SAFE and the step repeated, at most 3 times.")
def vt_conflict(c):
    tries = []
    for attempt in range(3):
        idx = c.now_idx()
        c.link.send(fdir.tc_set_mode("DEGRADED"), "ground: DEGRADED")
        c.link.send(fdir.tc_set_mode("NOMINAL"), "ground: NOMINAL, same cycle")
        c.wait(4)
        trans = [e for _, e in c.events(idx, fdir.EVT_MODE)]
        disc = [tm for tm, e in c.events(idx, EVT_DISCARD)]
        tries.append(len(trans))
        if len(trans) == 1:
            ok = (trans[0]["frm"] == SAFE and trans[0]["to"] == DEGRADED and len(disc) == 1
                  and disc[0].data[2] == EV_TC_MODE_NOMINAL and c.mode() == DEGRADED)
            return ok, (f"attempt {attempt + 1}: 1 transition SAFE -> "
                        f"{MODE.get(trans[0]['to'])}, {len(disc)} discard report(s) naming event "
                        f"{[d.data[2] for d in disc]}, mode {MODE.get(c.mode())}")
        c.command(fdir.tc_set_mode("SAFE"), "back to SAFE for a retry")
        c.wait(3)
    return False, f"commands always split across cycles: transitions per attempt {tries}"


@vt("VT-MM-020b", ["MM-020"], "Leaving SAFE does not resume the schedule; the ground does", "R2",
    "Vehicle in DEGRADED after SAFE; scheduled command overdue.",
    ["Wait 3 s.", "Send TC(11,1) enable the schedule.", "Wait 3 s."],
    "No TM(17,2) to 0x0042 before TC(11,1); exactly one after it.")
def vt_sched_resume(c):
    c.wait(3)
    before = sum(1 for t in c.since(c.mark) if t.kind == (17, 2) and t.dest_id == TAG)
    idx = c.now_idx()
    c.command(fdir.tc_sched_enable(), "enable the schedule")
    c.wait(3)
    after = sum(1 for t in c.since(idx) if t.kind == (17, 2) and t.dest_id == TAG)
    return before == 0 and after == 1, f"before enable: {before}; after enable: {after}"


# R3 - monitoring, inhibit -------------------------------------------------------------------

@vt("VT-FDIR-010", ["FDIR-010"], "Single sample glitch is filtered and counted", "R3",
    "Fresh spacecraft in NOMINAL.",
    ["Read HK filtered counter.", "Inject T_BAT 48 C for one sample.", "Observe 6 s."],
    "No monitor event, no transition; mode NOMINAL; filtered counter up by at least 1.")
def vt_glitch(c):
    c.boot("R3 monitoring and inhibit")
    _, h0 = c.last_hk(1)
    idx = c.now_idx()
    c.command(fdir.tc_inject_param("T_BAT", 4800, samples=1), "T_BAT 48 C, one sample")
    c.wait(6)
    _, h1 = c.last_hk(1)
    evs = [e for _, e in c.events(idx) if e["id"] in fdir.EVT_MON or e["id"] == fdir.EVT_MODE]
    filt = h1["filtered"] - h0["filtered"] if h0 and h1 else None
    return (not evs and c.mode() == NOMINAL and filt is not None and filt >= 1,
            f"{len(evs)} events; mode {MODE.get(c.mode())}; filtered +{filt}")


@vt("VT-FDIR-020", ["FDIR-020", "MM-040"], "Sustained T_BAT violation: one alarm, DEGRADED", "R3",
    "Vehicle in NOMINAL (1 Hz monitoring).",
    ["Inject T_BAT 48 C (sustained).", "Observe 12 s."],
    "One T_BAT alarm event no later than 4 s after TM(1,7) of the injection (persistence 3 + 1 "
    "sample periods); no second T_BAT alarm; transition NOMINAL to DEGRADED, cause MONITOR.")
def vt_tbat(c):
    idx = c.now_idx()
    done = c.command(fdir.tc_inject_param("T_BAT", 4800), "T_BAT 48 C, sustained")
    c.wait(12)
    alarms = [(tm, e) for tm, e in c.events(idx, 0x0220) if e.get("state") == 2]
    trans = [e for _, e in c.events(idx, fdir.EVT_MODE)]
    lat = alarms[0][0].time - done.time if alarms and done else None
    ok = (len(alarms) == 1 and lat is not None and lat <= 4.0 and len(trans) == 1
          and trans[0]["to"] == DEGRADED and trans[0]["src"] == CAUSE_MONITOR)
    return ok, (f"{len(alarms)} alarm event(s), first {lat:.3f} s after TM(1,7); transitions "
                f"{[(MODE[t['frm']], MODE[t['to']], fdir.CAUSE.get(t['src'])) for t in trans]}"
                if lat is not None else f"{len(alarms)} alarms, transitions {len(trans)}")


@vt("VT-MM-050", ["MM-050"], "No autonomous recovery when the parameter returns", "R3",
    "Vehicle in DEGRADED with T_BAT in alarm (VT-FDIR-020).",
    ["Clear the injection.", "Wait for the T_BAT cleared event; observe 10 s more."],
    "T_BAT cleared event; no transition; mode DEGRADED.")
def vt_no_recovery(c):
    idx = c.now_idx()
    c.command(fdir.tc_inject_clear(), "clear injection")
    cleared = c.link.wait_for(c.is_event(0x0220, state=0), 10)
    c.wait(10)
    trans = c.events(idx, fdir.EVT_MODE)
    return (cleared is not None and not trans and c.mode() == DEGRADED,
            f"cleared event {cleared is not None}; {len(trans)} transitions; mode "
            f"{MODE.get(c.mode())}")


@vt("VT-OBS-01", [], "Exploratory: ground returns to NOMINAL while an alarm is still active", "R3",
    "Vehicle in DEGRADED. Validation question, no requirement: what does the operator get?",
    ["Inject T_BAT 48 C (sustained); wait for the alarm event, then 3 s more (a later cycle).",
     "Send TC(8,1) NOMINAL.", "Observe 8 s.", "Clear the injection."],
    "Recorded, not judged: the resulting mode, the T_BAT monitor state in HK, and whether any event "
    "tells the operator that the alarm is still active.")
def vt_obs_alarm_active(c):
    c.command(fdir.tc_inject_param("T_BAT", 4800), "T_BAT 48 C, sustained")
    c.link.wait_for(c.is_event(0x0220, state=2), 10)
    c.wait(3)
    idx = c.now_idx()
    c.command(fdir.tc_set_mode("NOMINAL"), "ground: NOMINAL, alarm still active")
    c.wait(8)
    _, h = c.last_hk(1)
    evs = [(hex(e["id"]), e.get("state"), e.get("to")) for _, e in c.events(idx)]
    tb = fdir.MON_STATE.get(h["mon_states"][1]) if h else "?"
    c.command(fdir.tc_inject_clear(), "clear injection")
    c.link.wait_for(c.is_event(0x0220, state=0), 10)
    c.command(fdir.tc_set_mode("NOMINAL"), "ground: NOMINAL (settle)")
    c.wait(2)
    return None, (f"mode {MODE.get(h['mode']) if h else '?'}; T_BAT monitor {tb}; events after the "
                  f"command: {evs}")


@vt("VT-FDIR-090", ["FDIR-090"], "Per-parameter monitoring disable and enable", "R3",
    "Vehicle in NOMINAL.",
    ["TC(12,2) T_BAT.", "Inject T_BAT 48 C (sustained); observe 6 s.",
     "TC(12,1) T_BAT; observe 6 s.", "Clear; ground NOMINAL."],
    "No T_BAT event while disabled; one T_BAT alarm within 6 s of re-enabling.")
def vt_monitor_enable(c):
    c.command(fdir.tc_monitor(False, "T_BAT"), "disable T_BAT monitoring")
    idx = c.now_idx()
    c.command(fdir.tc_inject_param("T_BAT", 4800), "T_BAT 48 C, sustained")
    c.wait(6)
    while_off = len(c.events(idx, 0x0220))
    idx = c.now_idx()
    c.command(fdir.tc_monitor(True, "T_BAT"), "enable T_BAT monitoring")
    c.wait(6)
    after_on = len([1 for _, e in c.events(idx, 0x0220) if e.get("state") == 2])
    c.command(fdir.tc_inject_clear(), "clear injection")
    c.link.wait_for(c.is_event(0x0220, state=0), 10)
    c.command(fdir.tc_set_mode("NOMINAL"), "ground: NOMINAL")
    c.wait(3)
    return (while_off == 0 and after_on == 1,
            f"T_BAT events while disabled: {while_off}; alarms after enable: {after_on}")


@vt("VT-MM-060", ["MM-060", "MM-070"], "Inhibit: autonomous change refused and reported; ground "
    "still obeyed", "R3",
    "Vehicle in NOMINAL.",
    ["TC(19,9) inhibit 120 s.", "Inject T_BAT 48 C (sustained); observe 8 s.",
     "TC(8,1) DEGRADED; observe 4 s.", "TC(19,8); clear; ground NOMINAL."],
    "Alarm reported; ACTION INHIBITED event (kind mode change); mode stays NOMINAL. The ground "
    "command is applied: NOMINAL to DEGRADED, cause GROUND.")
def vt_inhibit(c):
    c.command(fdir.tc_fdir_inhibit(120), "inhibit FDIR 120 s")
    idx = c.now_idx()
    c.command(fdir.tc_inject_param("T_BAT", 4800), "T_BAT 48 C, sustained")
    c.wait(8)
    alarm = any(e.get("state") == 2 for _, e in c.events(idx, 0x0220))
    refused = [e for _, e in c.events(idx, fdir.EVT_ACTION_INHIBITED) if e.get("kind") == 2]
    mode_after_fault = c.mode()
    idx = c.now_idx()
    c.command(fdir.tc_set_mode("DEGRADED"), "ground: DEGRADED under inhibit")
    ev = c.link.wait_for(c.is_event(fdir.EVT_MODE, to=DEGRADED), 4)
    e = fdir.decode_event(ev) if ev else None
    c.command(fdir.tc_fdir_enable(), "lift the inhibit")
    c.command(fdir.tc_inject_clear(), "clear injection")
    c.link.wait_for(c.is_event(0x0220, state=0), 10)
    c.command(fdir.tc_set_mode("NOMINAL"), "ground: NOMINAL")
    c.wait(3)
    ok = alarm and refused and mode_after_fault == NOMINAL and e and e["src"] == CAUSE_GROUND
    return bool(ok), (f"alarm {alarm}; {len(refused)} refusal(s); mode after fault "
                      f"{MODE.get(mode_after_fault)}; ground DEGRADED applied "
                      f"{e is not None and e['src'] == CAUSE_GROUND}")


@vt("VT-FDIR-080", ["FDIR-080"], "Inhibit expires by itself and says so", "R3",
    "Vehicle in NOMINAL, FDIR enabled.",
    ["TC(19,9) inhibit 5 s.", "Wait up to 10 s for the inhibit OFF event."],
    "Inhibit ON event; OFF event with reason timeout 4.5 s to 7 s after TM(1,7); HK inhibit bit "
    "cleared.")
def vt_inhibit_expiry(c):
    done = c.command(fdir.tc_fdir_inhibit(5), "inhibit FDIR 5 s")
    off = c.link.wait_for(c.is_event(fdir.EVT_INHIBIT, on=0), 10)
    c.wait(1.5)
    e = fdir.decode_event(off) if off else None
    dt = off.time - done.time if off and done else None
    _, h = c.last_hk(1)
    ok = (e is not None and e["reason"] == REASON_TIMEOUT and dt is not None and 4.5 <= dt <= 7
          and h is not None and not h["status"] & fdir.ST_INHIBITED)
    return ok, (f"OFF after {dt:.3f} s, reason {e['reason']}; HK inhibited bit "
                f"{bool(h['status'] & fdir.ST_INHIBITED) if h else '?'}" if e else "no OFF event")


# R4 - under voltage ---------------------------------------------------------------------------

@vt("VT-FDIR-030", ["FDIR-030", "MM-040"], "Bus under voltage puts the vehicle in SAFE", "R4",
    "Fresh spacecraft in NOMINAL.",
    ["Inject V_BUS 25.0 V (sustained).", "Observe 8 s."],
    "V_BUS alarm (low); transition NOMINAL to SAFE with cause MONITOR, within 6 s of TM(1,7) "
    "(detection 4 s + one supervisor period).")
def vt_undervoltage(c):
    c.boot("R4 under voltage")
    idx = c.now_idx()
    done = c.command(fdir.tc_inject_param("V_BUS", 25000), "V_BUS 25.0 V, sustained")
    ev = c.link.wait_for(c.is_event(fdir.EVT_MODE, to=SAFE), 10)
    alarm = [e for _, e in c.events(idx, 0x0210) if e.get("state") == 2]
    e = fdir.decode_event(ev) if ev else None
    dt = ev.time - done.time if ev and done else None
    ok = alarm and e and e["src"] == CAUSE_MONITOR and dt is not None and dt <= 6.0
    return bool(ok), (f"V_BUS alarm {bool(alarm)}; SAFE after {dt:.3f} s, cause "
                      f"{fdir.CAUSE.get(e['src'])}" if e else f"alarm {bool(alarm)}, no SAFE")


# R5, R6 - liveness ----------------------------------------------------------------------------

@vt("VT-FDIR-040", ["FDIR-040"], "One heartbeat miss: restart, no mode change", "R5",
    "Fresh spacecraft in NOMINAL.",
    ["Inject a PL_LOG hang (once).", "Observe 8 s."],
    "HEARTBEAT MISSED and TASK RESTARTED events for PL_LOG; no transition; mode NOMINAL. Timing: the "
    "hang begins at PL_LOG's next release, which the ground cannot see, up to one PL_LOG period "
    "(1.0 s) after TM(1,7); so the restart must be reported no later than 5.0 s after TM(1,7) "
    "(4.0 s + 1.0 s).")
def vt_restart(c):
    c.boot("R5 single task hang")
    idx = c.now_idx()
    done = c.command(fdir.tc_inject_task_hang("PL_LOG", 1), "PL_LOG hang once")
    c.wait(8)
    missed = [tm for tm, e in c.events(idx, fdir.EVT_MISSED) if e.get("task") == 2]
    rest = [tm for tm, e in c.events(idx, fdir.EVT_RESTARTED) if e.get("task") == 2]
    trans = c.events(idx, fdir.EVT_MODE)
    dt = rest[0].time - done.time if rest and done else None
    ok = missed and rest and dt is not None and dt <= 5.0 and not trans and c.mode() == NOMINAL
    return bool(ok), (f"missed {len(missed)}, restarted {len(rest)} after "
                      f"{dt:.3f} s; {len(trans)} transitions" if dt is not None else
                      f"missed {len(missed)}, restarted {len(rest)}")


@vt("VT-FDIR-050", ["FDIR-050", "MM-040"], "Repeated failure: bounded restarts, escalation to "
    "SAFE", "R6",
    "Fresh spacecraft in NOMINAL.",
    ["Inject an ACS_CTRL hang five times.", "Observe until the escalation event, then 6 s more."],
    "Three TASK RESTARTED events, then TASK ESCALATED; transition to SAFE with cause LIVENESS; no "
    "restart after the escalation.")
def vt_escalation(c):
    c.boot("R6 repeated task failure")
    idx = c.now_idx()
    c.command(fdir.tc_inject_task_hang("ACS_CTRL", 5), "ACS_CTRL hang x5")
    esc = c.link.wait_for(c.is_event(fdir.EVT_ESCALATED, task=0), 30)
    c.wait(6)
    rest = [tm for tm, e in c.events(idx, fdir.EVT_RESTARTED) if e.get("task") == 0]
    late = [r for r in rest if esc is not None and r.time > esc.time]
    trans = [e for _, e in c.events(idx, fdir.EVT_MODE)]
    ok = (esc is not None and len(rest) == 3 and not late and trans
          and trans[-1]["to"] == SAFE and trans[-1]["src"] == CAUSE_LIVENESS)
    return ok, (f"{len(rest)} restarts, escalation {esc is not None}, {len(late)} restarts after "
                f"it; transitions {[(MODE[t['to']], fdir.CAUSE.get(t['src'])) for t in trans]}")


@vt("VT-FDIR-050b", ["FDIR-050"], "Repeated PL_LOG failure: escalation to DEGRADED, failed task "
    "flagged in HK", "R6b",
    "Fresh spacecraft in NOMINAL. Added after the coverage analysis (coverage.md, C-04).",
    ["Inject a PL_LOG hang five times.", "Observe until the escalation event, then 6 s more."],
    "Three TASK RESTARTED events, then TASK ESCALATED for PL_LOG; transition to DEGRADED with cause "
    "LIVENESS; no restart after the escalation; full HK shows PL_LOG (bit 2) in the failed mask.")
def vt_escalation_log(c):
    c.boot("R6b repeated PL_LOG failure")
    idx = c.now_idx()
    c.command(fdir.tc_inject_task_hang("PL_LOG", 5), "PL_LOG hang x5")
    esc = c.link.wait_for(c.is_event(fdir.EVT_ESCALATED, task=2), 40)
    c.wait(6)
    rest = [tm for tm, e in c.events(idx, fdir.EVT_RESTARTED) if e.get("task") == 2]
    late = [r for r in rest if esc is not None and r.time > esc.time]
    trans = [e for _, e in c.events(idx, fdir.EVT_MODE)]
    _, h = c.last_hk(1)
    failed = h["failed"] if h else None
    ok = (esc is not None and len(rest) == 3 and not late and trans
          and trans[-1]["to"] == DEGRADED and trans[-1]["src"] == CAUSE_LIVENESS
          and failed is not None and failed & 0x4)
    return bool(ok), (f"{len(rest)} restarts, escalation {esc is not None}, {len(late)} restarts "
                      f"after it; transitions {[(MODE[t['to']], fdir.CAUSE.get(t['src'])) for t in trans]};"
                      f" HK failed mask 0x{failed if failed is not None else 0:02X}")


# R7 - watchdog ---------------------------------------------------------------------------------

@vt("VT-FDIR-060", ["FDIR-060", "MM-090"], "Supervisor hang: watchdog warning, reset, boot in "
    "SAFE", "R7",
    "Fresh spacecraft in NOMINAL.",
    ["Inject a supervisor hang.", "Wait for the watchdog warning and the reset (process exit).",
     "Restart the spacecraft keeping the NV memory; read the boot report and HK."],
    "Warning no later than 6 s after TM(1,7); reset follows; boot report: last reset WATCHDOG, mode "
    "SAFE; HK cause WATCHDOG_RESET; no transition report at boot. On a target without persistent NV "
    "(SIS) the last part cannot be shown: N/A.")
def vt_watchdog(c):
    c.boot("R7 watchdog")
    done = c.command(fdir.tc_inject_supervisor_hang(), "supervisor hang")
    warn = c.link.wait_for(c.is_event(fdir.EVT_WDG), 12)
    reset = c.link.wait_exit(10)
    dt = warn.time - done.time if warn and done else None
    part1 = warn is not None and dt is not None and dt <= 6.0 and reset
    boot = c.boot("R7 after watchdog reset", keep_nv=True)
    c.wait(4)
    hk2 = next((t for t in c.since(c.mark) if t.kind == (3, 25) and t.data[0] == 2), None)
    h = fdir.decode_hk(hk2) if hk2 else None
    trans = c.events(c.mark, fdir.EVT_MODE)
    head = (f"warning after {dt:.3f} s, reset {reset}; " if dt is not None
            else f"warning {warn is not None}, reset {reset}; ")
    if c.a.nv_volatile and boot is not None and boot["nv_valid"] == 0:
        return ("N/A" if part1 else False), head + ("boot after reset: NV not preserved by this "
                                                     "target, cold boot")
    part2 = (boot is not None and boot["last_reset"] == 1 and boot["mode"] == SAFE and h is not None
             and h["mode"] == SAFE and h["cause_src"] == CAUSE_WDG and not trans)
    return part1 and part2, head + (f"boot report last reset {boot['last_reset'] if boot else '-'}, "
                                    f"mode {MODE.get(boot['mode']) if boot else '-'}; HK cause "
                                    f"{fdir.CAUSE.get(h['cause_src']) if h else '-'}; "
                                    f"{len(trans)} transition reports at boot")


# R8 - false positives ----------------------------------------------------------------------------

@vt("VT-FDIR-070", ["FDIR-070"], "No false alarm in nominal operation", "R8",
    "Fresh spacecraft in NOMINAL, no injection.",
    ["Observe for the false positive duration (300 s of onboard time by default)."],
    "No monitor alarm, heartbeat event, restart or transition; mode NOMINAL.")
def vt_false_positive(c):
    c.boot("R8 false positive run")
    start = c.onboard_now()
    deadline = time.monotonic() + c.a.fp_seconds * 3 + 60
    while time.monotonic() < deadline and c.onboard_now() - start < c.a.fp_seconds:
        c.wait(5)
    seen = c.onboard_now() - start
    bad = [hex(e["id"]) for _, e in c.events(c.mark)
           if e["id"] != fdir.EVT_BOOT and not (e["id"] in fdir.EVT_MON and e.get("state") == 0)]
    ok = seen >= c.a.fp_seconds * 0.99 and not bad and c.mode() == NOMINAL
    return ok, f"{seen:.0f} s observed; unexpected events {bad}; mode {MODE.get(c.mode())}"


# After all test build runs -----------------------------------------------------------------------

@vt("VT-TMTC-050", ["TMTC-050"], "Telemetry integrity over every run of the campaign", "END",
    "All runs R1 to R8 completed.",
    ["For each boot: check per-APID sequence counts.", "Count the lines the ground rejected.",
     "Read the TM drop counter in the last full HK of each boot."],
    "Every per-APID sequence advances by exactly 1; 0 lines rejected by the ground; TM drop 0.")
def vt_integrity(c):
    issues = []
    bounds = [s for s, _ in c.segments] + [len(c.link.telemetry)]
    packets = 0
    for (start, label), end in zip(c.segments, bounds[1:]):
        by = {}
        drop = None
        for tm in c.link.telemetry[start:end]:
            by.setdefault(tm.apid, []).append(tm.seq_count)
            if tm.kind == (3, 25) and tm.data[0] == 1:
                drop = fdir.decode_hk(tm)["tm_drop"]
        packets += end - start
        for apid, seqs in by.items():
            gaps = sum(1 for a, b in zip(seqs, seqs[1:]) if (b - a) & 0x3FFF != 1)
            if gaps:
                issues.append(f"{label}: APID 0x{apid:03X} {gaps} gap(s)")
        if drop:
            issues.append(f"{label}: TM drop {drop}")
    issues += [f"{len(c.link.errors)} line(s) rejected by the ground"] if c.link.errors else []
    return not issues, (f"{packets} packets in {len(c.segments)} boots; "
                        + ("; ".join(issues) if issues else "no gap, no rejection, no drop"))


# R9 - flight build ---------------------------------------------------------------------------------

@vt("VT-TMTC-070", ["TMTC-070"], "Flight build has no fault injection", "R9",
    "Fresh flight build in NOMINAL.",
    ["Send TC(200,1) T_BAT 48 C, TC(200,2), TC(200,3), TC(200,4), TC(200,99).", "Observe 5 s."],
    "Five TM(1,2) with code 7; no TM(1,7); no event; mode NOMINAL; T_BAT unchanged.")
def vt_flight(c):
    c.boot("R9 flight build", flight=True)
    idx = c.now_idx()
    codes = []
    for cmd in (fdir.tc_inject_param("T_BAT", 4800), fdir.tc_inject_task_hang("ACS_CTRL", 1),
                fdir.tc_inject_supervisor_hang(), fdir.tc_inject_clear(), (200, 99, b"")):
        tc, final, _ = c.send(cmd, "fault injection on the flight build")
        codes.append(final.data[4] if final is not None and final.subtype == 2 else
                     (f"TM(1,{final.subtype})" if final else None))
    c.wait(5)
    evs = [hex(e["id"]) for _, e in c.events(idx)]
    _, h = c.last_hk(1)
    ok = codes == [7] * 5 and not evs and h is not None and h["mode"] == NOMINAL and h["tbat"] < 45
    return ok, (f"codes {codes}; events {evs}; mode {MODE.get(h['mode']) if h else '-'}, T_BAT "
                f"{h['tbat'] if h else '-'} C")


# Analysis and lower level verification, recorded in the campaign -------------------------------

@vt("VT-MM-100", ["MM-100"], "Completeness of the mode model (analysis)", "A",
    "Model vehicle_mode v3.",
    ["Read the generator's completeness report and its negative test results."],
    "0 unhandled (state, event) pairs; generator negative tests all pass.")
def vt_completeness(c):
    rep = open(os.path.join(ROOT, "labs", "session-06", "lab-6.1-model", "completeness.md")).read()
    gen = open(os.path.join(ROOT, "labs", "session-06", "lab-6.2-generation", "tests",
                            "results.txt")).read()
    import re
    m = re.search(r"(\d+) unhandled", rep)
    unhandled = int(m.group(1)) if m else None
    gen_ok = re.search(r"\d+ cases, 0 failed", gen) is not None
    return (unhandled == 0 and gen_ok,
            f"{unhandled} unhandled pairs (completeness.md); generator tests pass {gen_ok}")


@vt("VT-MM-130", ["MM-130"], "Fault in the same cycle as a ground recovery (integration level)",
    "A",
    "Not reproducible through the ground interface: the ground cannot choose the evaluation cycle "
    "a command lands in.",
    ["Read the integration suite result IT-10 (tests/integration, same flight code)."],
    "IT-10 PASS.")
def vt_latched_fault(c):
    path = c.a.integration_log
    if not path or not os.path.exists(path):
        return "N/A", "integration log not available"
    txt = open(path, errors="replace").read()
    import re
    m = re.search(r"^IT-10\s.*\s(PASS|FAIL)\s*$", txt, re.M)
    return (m is not None and m.group(1) == "PASS",
            f"IT-10 {m.group(1) if m else 'not found'} ({os.path.basename(path)})")


# ---- Output -----------------------------------------------------------------------------------------

RUN_TITLES = {"R1": "R1 - TM/TC basics", "R2": "R2 - ground mode commands, SAFE",
              "R3": "R3 - monitoring and inhibit", "R4": "R4 - under voltage",
              "R5": "R5 - single task hang", "R6": "R6 - repeated task failure",
              "R6b": "R6b - repeated payload task failure",
              "R7": "R7 - watchdog", "R8": "R8 - false positive run",
              "END": "After all test build runs", "R9": "R9 - flight build",
              "A": "Analysis and integration level evidence"}


def write_spec(path):
    lines = ["# Validation test specification", "",
             "Generated from `tests/validation/run_campaign.py`: the specification below is the same "
             "data the campaign executes. Each run starts a fresh spacecraft with blank NV memory "
             "unless stated; tests in a run execute in the order listed and some hand state to the "
             "next, as their preconditions say.", "",
             "Verdicts: PASS, FAIL, N/A (cannot be shown on this target, with the reason), "
             "OBSERVED (exploratory test, no requirement, result recorded for validation).", ""]
    run = None
    for t in TESTS:
        if t["run"] != run:
            run = t["run"]
            lines += [f"## {RUN_TITLES[run]}", ""]
        lines += [f"### {t['id']}: {t['title']}", "",
                  f"- **Requirements:** {', '.join(t['reqs']) or 'none (exploratory)'}",
                  f"- **Preconditions:** {t['pre']}",
                  "- **Steps:**"] + [f"  {i}. {s}" for i, s in enumerate(t["steps"], 1)] + [
                  f"- **Expected:** {t['expected']}", ""]
    with open(path, "w") as f:
        f.write("\n".join(lines))


def write_results(c, out, started, elapsed):
    res = c.results
    counts = {}
    for r in res.values():
        counts[r["verdict"]] = counts.get(r["verdict"], 0) + 1
    lines = [f"# Validation campaign results: {c.a.target}", "",
             f"- Executed: {started} UTC, {elapsed / 60:.1f} min",
             f"- Spacecraft: `{os.path.basename(c.a.spawn.split()[-1])}` (test build), "
             f"`{os.path.basename(c.a.spawn_flight.split()[-1])}` (flight build)",
             f"- Log: `campaign.log` (every TC and TM, decoded)",
             f"- Verdicts: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())), "",
             "| Test | Requirements | Expected | Actual | Verdict |", "|---|---|---|---|---|"]
    for t in TESTS:
        r = res.get(t["id"])
        if r is None:
            continue
        lines.append(f"| {t['id']} | {', '.join(t['reqs']) or '-'} | {t['expected']} | "
                     f"{r['actual']} | **{r['verdict']}** |")
    lines.append("")
    with open(os.path.join(out, "results.md"), "w") as f:
        f.write("\n".join(lines))
    with open(os.path.join(out, "results.json"), "w") as f:
        json.dump(dict(target=c.a.target, executed=started, minutes=round(elapsed / 60, 1),
                       results=res), f, indent=1)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    Link.add_arguments(p)
    p.add_argument("--spawn-flight", default="")
    p.add_argument("--target", default="host-sim")
    p.add_argument("--out", required=True)
    p.add_argument("--fp-seconds", type=float, default=300)
    p.add_argument("--nv-volatile", action="store_true")
    p.add_argument("--only", default="", help="comma separated runs, e.g. R1,R3")
    p.add_argument("--integration-log", default=os.path.join(ROOT, "tests", "build",
                                                             "integration",
                                                             "int_tc_path.host.log"))
    p.add_argument("--spec-only", action="store_true")
    a = p.parse_args()
    spec = os.path.join(ROOT, "labs", "session-07", "validation", "test-spec.md")
    write_spec(spec)
    if a.spec_only:
        print(f"wrote {spec}")
        return 0
    os.makedirs(a.out, exist_ok=True)
    a.log = os.path.join(a.out, "campaign.log")
    a.nv_file = os.path.join(a.out, "nv.bin")
    only = {r for r in a.only.split(",") if r}
    c = Campaign(a)
    started = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M")
    t0 = time.monotonic()
    for t in TESTS:
        if only and t["run"] not in only:
            continue
        if t["run"] == "END" and only:
            continue
        try:
            ok, actual = t["fn"](c)
        except Exception as exc:      # a crashed test is a failed test, with the reason
            ok, actual = False, f"test raised {type(exc).__name__}: {exc}"
        verdict = ("OBSERVED" if ok is None else ok if isinstance(ok, str)
                   else "PASS" if ok else "FAIL")
        c.results[t["id"]] = dict(reqs=t["reqs"], verdict=verdict, actual=actual)
        if c.link is not None:
            c.link.log(f"# {t['id']} {verdict}: {actual}")
        else:
            print(f"# {t['id']} {verdict}: {actual}")
    if c.link is not None:
        c.link.close()
    if os.path.exists(a.nv_file):
        os.remove(a.nv_file)
    write_results(c, a.out, started, time.monotonic() - t0)
    fails = [k for k, r in c.results.items() if r["verdict"] == "FAIL"]
    print(f"campaign: {len(c.results)} tests, {len(fails)} failed {fails}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
