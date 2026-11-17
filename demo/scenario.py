"""LOTUS-1 mission demonstration, scripted and checked (capstone Phase 2).

The seven-step sequence of the capstone brief, run against the flight
software through the ground security unit, with a PASS/FAIL check and a
measured time for every step. The same sequence is run from the mission
control console by demo/run_demo.py (for the recording); this script is the
repeatable, automatic version, and its log is evidence (evidence/demo/).

    python3 demo/scenario.py --spawn build/host/lotus1-test.host --log evidence/demo/scenario-host-sim.log
    python3 demo/scenario.py --pty --spawn "sparc-rtems6-sis -leon3 -rt -r build/rtems/lotus1-test.exe" ...

Steps:
  1 boot to NOMINAL, housekeeping flowing (first contact on a factory key store)
  2 authenticated TC: payload ON -> TM(1,1), TM(1,7), payload running
  3 TC with an invalid authentication tag (payload OFF) -> rejected, security
    event, payload state unchanged
  4 thermal anomaly (T_BAT forced to 48 C): persistence holds 3 samples, ONE
    monitor event, DEGRADED, payload suspended
  5 thermal recovery attempts exhaust (3, 10 s apart) -> SAFE: payload off,
    schedule suspended, essential telemetry only
  6 recovery procedure SAFE-01: assess, clear the fault, authenticated SAFE
    exit, NOMINAL confirmed, payload and schedule back
"""
import argparse
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "ground"))

import fdir       # noqa: E402
import secdecode  # noqa: E402
import seclink    # noqa: E402
from link import Checker  # noqa: E402

T_ALARM = 4800          # 48.00 C, above the 45.00 C high limit
T_CLEAR = 43.00         # the monitor's clear threshold, C
T_HIGH = 45.00          # the monitor's high limit, C


class Scenario:
    def __init__(self, args):
        self.link = seclink.SecureLink(args)
        self.c = Checker(self.link)
        self.results = []          # (step, check, ok, measured)
        self.step_name = ""

    # -- helpers --------------------------------------------------------------
    def step(self, n, text):
        self.step_name = f"{n}"
        self.link.log(f"# ===== STEP {n}: {text}")
        print(f"\n== STEP {n}: {text}", flush=True)

    def check(self, ok, text, measured=None):
        self.c.check(ok, f"[{self.step_name}] {text}" + (f"  ({measured})" if measured else ""))
        self.results.append({"step": self.step_name, "check": text, "ok": bool(ok),
                             "measured": measured})
        return ok

    def t(self):
        return time.monotonic()

    def hk(self, timeout=3.0, sid=None):
        tm = self.link.wait_for(lambda m: m.apid == fdir.APID_HK and m.kind == (3, 25) and
                                (sid is None or m.data[0] == sid), timeout)
        return fdir.decode_hk(tm) if tm else None

    def events(self, since, eid=None):
        out = []
        for m in self.link.telemetry[since:]:
            if m.apid == fdir.APID_EVENT and m.service == 5:
                e = fdir.decode_event(m)
                if eid is None or e["id"] == eid:
                    e["_rx"] = m.rx_time if hasattr(m, "rx_time") else None
                    out.append(e)
        return out

    def wait_event(self, eid, since, timeout):
        m = self.link.wait_for(lambda x: x.apid == fdir.APID_EVENT and x.service == 5 and
                               struct.unpack(">H", x.data[:2])[0] == eid, timeout)
        return m

    def ack_time(self, tc, subtype=7):
        """Onboard time of the TM(1,subtype) for this TC."""
        for m in self.link.telemetry:
            if m.service == 1 and m.subtype == subtype and m.data[:4] == tc[:4]:
                return m.time
        return None

    def command(self, cmd, note, timeout=4.0, **kw):
        since = len(self.link.telemetry)
        t0 = self.t()
        tc = self.link.send(cmd, note, **kw)
        self.link.wait_for(lambda m: m.service == 1 and m.data[:4] == tc[:4] and
                           m.subtype in (2, 7, 8), timeout)
        subs = [m.subtype for m in self.link.telemetry[since:]
                if m.service == 1 and m.data[:4] == tc[:4]]
        return subs, self.t() - t0, tc

    # -- the sequence ------------------------------------------------------------
    def run(self):
        L = self.link

        self.step(1, "boot to NOMINAL, housekeeping flowing")
        t0 = self.t()
        h = self.hk(4.0, sid=1)
        self.check(h is not None and fdir.MODE[h["mode"]] == "NOMINAL",
                   "NOMINAL after boot, HK SID 1 flowing", f"{self.t() - t0:.1f} s to first HK")
        self.check(not (h["status"] & fdir.ST_PAYLOAD), "payload OFF at boot (until commanded)")
        fc = L.first_contact()
        self.check(True, f"key store: first contact {'done (factory state)' if fc else 'not needed'}")

        self.step(2, "authenticated TC: payload ON")
        subs, dt, _ = self.command(fdir.tc_payload(1), "PAYLOAD ON (authenticated)")
        self.check(subs == [1, 7], "TM(1,1) acceptance then TM(1,7) completion",
                   f"{dt * 1000:.0f} ms round trip")
        L.collect(1.2)
        h = self.hk(2.0, sid=1)
        self.check(h and h["status"] & fdir.ST_PAYLOAD and h["status"] & fdir.ST_PAYLOAD_ENABLED,
                   "HK: payload enabled and running")

        self.step(3, "TC with an invalid authentication tag")
        since = len(L.telemetry)
        bad = bytes(32)                               # not the spacecraft's key
        tc = L.send(fdir.tc_payload(0), "PAYLOAD OFF, FORGED (wrong key)", key=bad)
        L.collect(2.5)
        replies = [m for m in L.telemetry[since:] if m.service == 1 and m.data[:4] == tc[:4]]
        self.check(not replies, "rejected: no TM(1,x) at all (no oracle)")
        sec = L.sec_events(since, "REJECT_FIRST")
        self.check(sec and sec[0]["cls"] == "AUTH", "security event REJECT_FIRST, class AUTH",
                   f"code {sec[0]['code'] if sec else '-'}")
        h = self.hk(2.0, sid=1)
        self.check(h and h["status"] & fdir.ST_PAYLOAD, "payload state unchanged (still ON)")

        self.step(4, "thermal anomaly: T_BAT forced to 48.00 C")
        since = len(L.telemetry)
        subs, _, tc = self.command(fdir.tc_inject_param("T_BAT", T_ALARM, 0),
                                   "INJECT T_BAT 48.00 C (test build)")
        ob_inj = self.ack_time(tc)                     # onboard time of TM(1,7)
        m = self.wait_event(0x0220, since, 10.0)
        ob_mon = m.time if m else None
        self.check(m is not None, "monitor event T_BAT ALARM after persistence",
                   f"{ob_mon - ob_inj:.2f} s onboard after the injection (3 samples at 1 Hz)"
                   if m and ob_inj is not None else None)
        m = self.wait_event(fdir.EVT_MODE, since, 5.0)
        ob_deg = m.time if m else None
        t_deg = self.t()
        e = fdir.decode_event(m) if m else {}
        self.check(e.get("to") == 1, "mode NOMINAL -> DEGRADED, cause MONITOR T_BAT",
                   f"{ob_deg - ob_mon:.2f} s after the monitor event (2 s mode cycle)"
                   if m and ob_mon is not None else None)
        L.collect(1.5)
        mon = [x for x in self.events(since, 0x0220) if x["state"] == 2]
        self.check(len(mon) == 1, "exactly ONE monitor ALARM event (no repeats)", f"{len(mon)}")
        h = self.hk(2.0, sid=1)
        self.check(h and not h["status"] & fdir.ST_PAYLOAD and h["status"] & fdir.ST_PAYLOAD_ENABLED,
                   "payload suspended (enable kept), DEGRADED full HK at 2 Hz")

        self.step(5, "thermal recovery attempts exhaust -> SAFE")
        m = self.wait_event(fdir.EVT_THERM_EXHAUSTED, since, 40.0)
        ob_exh = m.time if m else None
        att = self.events(since, fdir.EVT_THERM_ATTEMPT)
        self.check(len(att) == 3 and [a["attempt"] for a in att] == [1, 2, 3],
                   "3 recovery attempts, then EXHAUSTED",
                   f"exhausted {ob_exh - ob_deg:.2f} s after DEGRADED" if m and ob_deg else None)
        m = self.wait_event(fdir.EVT_MODE, len(L.telemetry) - 3, 5.0)
        ob_safe = m.time if m else None
        L.collect(1.0)
        modes = [x for x in self.events(since, fdir.EVT_MODE) if x["to"] == 2]
        self.check(modes and modes[0]["frm"] == 1 and modes[0]["src"] == 2,
                   "mode DEGRADED -> SAFE, cause MONITOR",
                   f"{ob_safe - ob_inj:.2f} s from injection to SAFE, onboard time"
                   if ob_safe and ob_inj is not None else None)
        self.check(self.events(since, fdir.EVT_SCHED_SUSPENDED), "command schedule suspended")
        i_safe = len(L.telemetry)
        L.collect(6.5)
        recent = [m for m in L.telemetry[i_safe:] if m.apid == fdir.APID_HK and m.kind == (3, 25)]
        self.check(recent and all(m.data[0] == 2 for m in recent) and len(recent) <= 4,
                   "telemetry reduced to essentials (SID 2 only, every 2 s)",
                   f"{len(recent)} HK packets in 6.5 s, all SID 2")
        h = self.hk(4.0, sid=2)
        self.check(h and not h["status"] & (fdir.ST_PAYLOAD | fdir.ST_PAYLOAD_ENABLED)
                   and not h["status"] & fdir.ST_SCHED, "payload OFF (enable cleared), schedule off")
        subs, _, _ = self.command(fdir.tc_payload(1), "PAYLOAD ON while SAFE (must be refused)")
        self.check(subs == [1, 8], "payload enable in SAFE refused: TM(1,8)")

        self.step(6, "recovery procedure SAFE-01")
        h = self.hk(4.0, sid=2)
        self.check(h and fdir.MODE[h["mode"]] == "SAFE" and fdir.CAUSE[h["cause_src"]] == "MONITOR"
                   and h["tbat"] > T_HIGH,
                   "assess: SAFE, cause MONITOR, T_BAT above its limit in essential HK",
                   f"T_BAT {h['tbat']:.2f} C" if h else None)
        subs, _, _ = self.command(fdir.tc_inject_clear(), "CLEAR the injected fault (test build)")
        t_clr = self.t()
        ok = False
        while self.t() - t_clr < 20:
            h = self.hk(3.0, sid=2)
            if h and h["tbat"] <= T_CLEAR:
                ok = True
                break
        self.check(ok, "fault condition cleared: T_BAT below the 43.00 C clear threshold",
                   f"T_BAT {h['tbat']:.2f} C" if h else None)
        since = len(L.telemetry)
        subs, dt, tc = self.command(fdir.tc_set_mode("NOMINAL"), "SET_MODE NOMINAL (SAFE exit)")
        self.check(subs == [1, 7], "authenticated SAFE exit command accepted and completed")
        ob_cmd = self.ack_time(tc)
        m = self.wait_event(fdir.EVT_MODE, since, 5.0)
        e = fdir.decode_event(m) if m else {}
        self.check(e.get("frm") == 2 and e.get("to") == 0 and e.get("src") == 1,
                   "mode SAFE -> NOMINAL, cause GROUND",
                   f"{m.time - ob_cmd:.2f} s after TM(1,7) (MM-150: <= 3.0 s)"
                   if m and ob_cmd is not None else None)
        subs, _, _ = self.command(fdir.tc_payload(1), "PAYLOAD ON")
        subs2, _, _ = self.command(fdir.tc_sched_enable(), "SCHEDULE ENABLE")
        L.collect(1.5)
        h = self.hk(3.0, sid=1)
        self.check(h and fdir.MODE[h["mode"]] == "NOMINAL" and h["status"] & fdir.ST_PAYLOAD
                   and h["status"] & fdir.ST_SCHED and subs == [1, 7] and subs2 == [1, 7],
                   "NOMINAL confirmed in full HK, payload running, schedule enabled")
        sid3 = L.latest_sid3(3.0)
        self.check(sid3 and sid3["rejected"]["AUTH"] == 1,
                   "security HK: exactly one AUTH rejection over the whole run")
        self.c.check_sequences()


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    seclink.SecureLink.add_arguments(p)
    p.add_argument("--json", help="write the step results as JSON")
    p.set_defaults(nv_file=os.path.join(os.environ.get("TMPDIR", "/tmp"), "lotus1_demo_nv.bin"))
    args = p.parse_args()
    for f in (args.nv_file, args.nv_file + ".sec", args.nv_file + ".ground.json"):
        if os.path.exists(f):
            os.remove(f)
    s = Scenario(args)
    try:
        s.run()
    finally:
        if args.json:
            os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
            with open(args.json, "w") as f:
                json.dump(s.results, f, indent=1)
    s.c.finish()


if __name__ == "__main__":
    main()
