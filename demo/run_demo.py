#!/usr/bin/env python3
"""LOTUS-1 mission demonstration through the MCS (Yamcs), for the recording.

Drives the 7 steps of the capstone scenario through Yamcs' REST API, so
everything the viewer sees happens in the MCS: commands in Command history,
parameters on operator.par, events in the Events view. Prints one caption
per step in the terminal (put the terminal in a corner of the recording, or
use the captions as subtitles: --srt writes them with their times).

Needs, in this order:
    mcs/run-bridge.sh fresh        terminal 1 (spacecraft + ground security unit)
    mcs/run-yamcs.sh               terminal 2 (http://localhost:8090, instance lotus1)
    python3 demo/run_demo.py       terminal 3

Step 3 sends a forged command on the bridge's raw uplink port (10026), which
stands for a transmitter on the uplink frequency: it does not go through the
security unit and has a trailer computed with a key the attacker guessed.

Standard library only, plus ground/ (pus, secunit). --pace scales every
pause (1.0 = recording speed, about 3:45; 0.3 for a dry run).
"""
import argparse
import json
import os
import socket
import sys
import tempfile
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "ground"))
import pus      # noqa: E402
import secunit  # noqa: E402

P = argparse.ArgumentParser(description=__doc__.split("\n")[0])
P.add_argument("--url", default="http://localhost:8090")
P.add_argument("--instance", default="lotus1")
P.add_argument("--raw", default="127.0.0.1:10026", help="bridge raw uplink host:port")
P.add_argument("--pace", type=float, default=1.0, help="pause scale (1.0 recording speed)")
P.add_argument("--srt", help="write the captions as an .srt subtitle file")
P.add_argument("--commit", default=None, help="commit shown in step 7 (default: git HEAD)")
A = P.parse_args()
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
T0 = time.time()
CAPTIONS = []
RESULTS = []


# ---- Yamcs REST -------------------------------------------------------------------------

def api(method, path, body=None):
    req = urllib.request.Request(A.url + "/api" + path, method=method,
                                 data=None if body is None else json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    with OPENER.open(req, timeout=10) as r:
        txt = r.read().decode()
        return json.loads(txt) if txt else {}


def param(name):
    v = api("GET", f"/processors/{A.instance}/realtime/parameters/LOTUS1/{name}")
    e = v.get("engValue", {})
    if "booleanValue" in e:
        return "ON" if e["booleanValue"] else "OFF"
    for k in ("stringValue", "uint32Value", "sint32Value", "floatValue", "doubleValue",
              "uint64Value", "sint64Value"):
        if k in e:
            return e[k]
    return None


def history(cid):
    for c in api("GET", f"/archive/{A.instance}/commands?limit=30").get("commands", []):
        if c["id"] == cid:
            return {a["name"]: next((v for k, v in a["value"].items() if k != "type"), None)
                    for a in c.get("attr", [])}
    return {}


def command(name, args=None, key="CommandComplete_Status", timeout=15, supervised=False):
    r = api("POST", f"/processors/{A.instance}/realtime/commands/LOTUS1/{name}",
            {"args": args or {}})
    cid = r["id"]
    if supervised or r.get("queue") == "supervised":
        api("POST", f"/processors/{A.instance}/realtime/queues/supervised/commands/"
                    f"{cid}:accept", {})
    deadline = time.time() + timeout
    h = {}
    while time.time() < deadline:
        h = history(cid)
        if h.get(key) not in (None, "PENDING"):
            break
        time.sleep(0.5)
    return h


def n_commands():
    return len(api("GET", f"/archive/{A.instance}/commands?limit=200").get("commands", []))


def events_since(t_iso):
    evs = api("GET", f"/archive/{A.instance}/events?limit=200").get("events", [])
    return [e.get("message", "") for e in evs if e.get("generationTime", "") >= t_iso]


def iso_now(back=0.0):
    t = time.time() - back
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(t)) + f".{int(t % 1 * 1000):03d}Z"


def wait_param(name, want, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        v = param(name)
        if (want(v) if callable(want) else v == want):
            return v
        time.sleep(0.5)
    return param(name)


# ---- presentation -------------------------------------------------------------------------

def caption(text, seconds):
    t = time.time() - T0
    CAPTIONS.append((t, t + seconds * max(A.pace, 0.3), text))
    print("\n" + "=" * 78 + f"\n  {text}\n" + "=" * 78, flush=True)


def pause(s):
    time.sleep(s * A.pace)


def check(ok, text):
    RESULTS.append((ok, text))
    print(("   PASS  " if ok else "   FAIL  ") + text, flush=True)
    return ok


def write_srt(path):
    def ts(x):
        ms = int(round(x * 1000))
        return f"{ms // 3600000:02d}:{ms // 60000 % 60:02d}:{ms // 1000 % 60:02d},{ms % 1000:03d}"
    with open(path, "w") as f:
        for i, (a, b, txt) in enumerate(CAPTIONS, 1):
            f.write(f"{i}\n{ts(a)} --> {ts(b)}\n{txt}\n\n")


def forged_payload_off():
    """TC(8,1) payload OFF with a trailer under a guessed key, on the raw uplink."""
    plain = pus.build_tc(0x3FF0, 8, 1, bytes([2, 0]))
    with tempfile.TemporaryDirectory() as d:
        attacker = secunit.SecurityUnit(os.path.join(d, "attacker.json"))
        pkt = attacker.protect(plain, spi=1, seq=0x7FFF0000, key=bytes(range(32)))
    host, port = A.raw.split(":")
    with socket.create_connection((host, int(port)), timeout=5) as s:
        s.sendall(pkt)
    return pkt


# ---- the scenario ---------------------------------------------------------------------------

def main():
    links = {l["name"]: l for l in api("GET", f"/links/{A.instance}")["links"]}
    if not (links["tm_realtime"]["status"] == "OK" and links["tc_realtime"]["status"] == "OK"):
        sys.exit("Yamcs links not OK: start mcs/run-bridge.sh fresh, then mcs/run-yamcs.sh")
    if param("sec_flag_slot1_untrusted") or param("sec_flag_slot2_untrusted"):
        print("# first contact (factory key store): RECOVERY_SET_COUNTER through the "
              "supervised queue")
        slot = param("sec_active_slot") or 1
        command("RECOVERY_SET_COUNTER", {"slot": f"SLOT_{slot}", "floor": 0}, supervised=True)
        time.sleep(2)
    if param("mode") != "NOMINAL":
        command("TEST_INJECT_CLEAR")
        command("SET_MODE", {"mode": "NOMINAL"}, key="Verifier_Complete_Status")
    if param("payload_enabled") == "ON":
        command("PAYLOAD", {"state": "OFF"})
    command("SCHED_ENABLE")

    # 1 ----------------------------------------------------------------------------------------
    caption("1/7  LOTUS-1 in NOMINAL: housekeeping every second to the MCS", 20)
    up0 = param("uptime")
    pause(6)
    up1 = param("uptime")
    check(param("mode") == "NOMINAL", "mode NOMINAL (HK SID 1 decoded by the MDB)")
    check(up0 is not None and up1 is not None and up1 > up0,
          f"HK live: uptime {up0} -> {up1} s; T_BAT {param('tbat')} C, V_BUS {param('vbus')} mV")
    pause(6)

    # 2 ----------------------------------------------------------------------------------------
    caption("2/7  Authenticated TC: payload ON. Acceptance TM(1,1), completion TM(1,7)", 25)
    rej0 = param("sec_rej_auth") or 0
    h = command("PAYLOAD", {"state": "ON"})
    check(h.get("Verifier_Accepted_Status") == "OK" and h.get("CommandComplete_Status") == "OK",
          "PAYLOAD ON: Accepted OK (TM(1,1)), Complete OK (TM(1,7)); signed by the "
          "ground security unit")
    on = wait_param("payload_on", "ON", 4)
    check(on == "ON" and param("payload_enabled") == "ON", "payload_enabled ON, payload_on ON")
    pause(10)

    # 3 ----------------------------------------------------------------------------------------
    caption("3/7  Forged TC (payload OFF, wrong key) on the raw uplink: rejected, event, "
            "payload unchanged", 30)
    t3 = iso_now(1)
    before = n_commands()
    forged_payload_off()
    pause(4)
    rej1 = wait_param("sec_rej_auth", lambda v: (v or 0) > rej0, 6)
    check((rej1 or 0) == rej0 + 1, f"SID 3 sec_rej_auth {rej0} -> {rej1}")
    check(param("payload_on") == "ON", "payload still ON")
    check(n_commands() == before, "no verification report: nothing in Command history")
    ev = events_since(t3)
    check(any(m.startswith("SECURITY") for m in ev), "security event: " +
          next((m for m in ev if m.startswith("SECURITY")), "none"))
    pause(10)

    # 4 ----------------------------------------------------------------------------------------
    caption("4/7  Thermal anomaly: T_BAT 48 C. Persistence filter, one event, DEGRADED, "
            "payload suspended", 30)
    t4 = iso_now(1)
    command("TEST_INJECT_PARAM", {"param": "T_BAT", "value": 4800, "samples": 0})
    mode = wait_param("mode", "DEGRADED", 8)
    ev = events_since(t4)
    alarms = [m for m in ev if "T_BAT" in m and "ALARM" in m]
    check(mode == "DEGRADED", "mode DEGRADED (cause MONITOR)")
    check(len(alarms) == 1, f"one T_BAT alarm event after 3 samples: {alarms[:1]}")
    check(wait_param("payload_on", "OFF", 3) == "OFF" and param("payload_enabled") == "ON",
          "payload suspended (payload_on OFF, ground enable kept)")
    pause(8)

    # 5 ----------------------------------------------------------------------------------------
    caption("5/7  Recovery attempts 1, 2, 3 (10 s apart), exhausted: SAFE. Payload off, "
            "schedule suspended, essential TM", 45)
    mode = wait_param("mode", "SAFE", 45)
    ev = events_since(t4)
    att = [m for m in ev if "recovery attempt" in m]
    check(len(att) == 3, f"{len(att)} THERMAL_RECOVERY_ATTEMPT events")
    check(any("EXHAUSTED" in m for m in ev), "THERMAL_RECOVERY_EXHAUSTED event")
    check(mode == "SAFE", "mode SAFE (cause MONITOR)")
    check(param("payload_on") == "OFF" and param("payload_enabled") == "OFF"
          and param("sched_enabled") == "OFF",
          "payload off, ground enable cleared, schedule suspended")
    h = command("PAYLOAD", {"state": "ON"})
    check(h.get("CommandComplete_Status") not in ("OK",),
          "PAYLOAD ON in SAFE refused (TM(1,8))")
    pause(8)

    # 6 ----------------------------------------------------------------------------------------
    caption("6/7  Recovery procedure SAFE-01: assess, clear the fault, authenticated "
            "SAFE exit, confirm NOMINAL", 40)
    check(param("mode_cause") == "MONITOR", f"assess: cause {param('mode_cause')}, "
          f"T_BAT {param('tbat')} C, boot count {param('boot_count')}")
    command("TEST_INJECT_CLEAR")
    tb = wait_param("tbat", lambda v: v is not None and v < 43.0, 10)
    check(tb is not None and tb < 43.0, f"fault cleared: T_BAT {tb} C, monitor "
          f"{param('mon_state_tbat')}")
    h = command("SET_MODE", {"mode": "NOMINAL"}, key="Verifier_Complete_Status", timeout=20)
    check(h.get("Verifier_PUS_Completed_Status") == "OK", "SET_MODE NOMINAL: authenticated, "
          "TM(1,7)")
    check(wait_param("mode", "NOMINAL", 5) == "NOMINAL", "confirmed: mode NOMINAL in HK")
    command("SCHED_ENABLE")
    pause(8)

    # 7 ----------------------------------------------------------------------------------------
    commit = A.commit
    if commit is None:
        try:
            import subprocess
            commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=HERE,
                                    capture_output=True, text=True).stdout.strip() or "?"
        except OSError:
            commit = "?"
    caption(f"7/7  Same commit {commit}: CI green (fast-checks), coverage report "
            f"(show the GitHub Actions run and evidence/coverage/coverage.md)", 25)
    ok = sum(1 for r in RESULTS if r[0])
    print(f"\n# demo checks: {ok}/{len(RESULTS)} passed, {time.time() - T0:.0f} s")
    if A.srt:
        write_srt(A.srt)
        print(f"# captions written to {A.srt}")
    return 0 if ok == len(RESULTS) else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except urllib.error.URLError as exc:
        sys.exit(f"Yamcs not reachable at {A.url}: {exc}")
