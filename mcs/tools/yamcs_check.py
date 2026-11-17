"""Lab 8.3 - end to end check through Yamcs (REST API, standard library only).

Needs Yamcs (../run-yamcs.sh) and the bridge with a spacecraft
(../run-bridge.sh fresh) running. Checks that Yamcs decodes live TM with
the MDB, that authenticated TCs sent from Yamcs come back with their
verification stages, that events are decoded, and that a rekey can be
done from Yamcs through the supervised queue.

    python3 yamcs_check.py [--url http://localhost:8090] [--rekey]
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "ground"))
import secunit  # noqa: E402

P = argparse.ArgumentParser()
P.add_argument("--url", default="http://localhost:8090")
P.add_argument("--instance", default="lotus1")
P.add_argument("--rekey", action="store_true", help="also load and activate a new key")
A = argparse.Namespace(url="http://localhost:8090", instance="lotus1", rekey=False)
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
passed = failed = 0


def api(method, path, body=None):
    req = urllib.request.Request(A.url + "/api" + path, method=method,
                                 data=None if body is None else json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    with OPENER.open(req, timeout=10) as r:
        txt = r.read().decode()
        return json.loads(txt) if txt else {}


def check(cond, text):
    global passed, failed
    print(("  PASS  " if cond else "  FAIL  ") + text, flush=True)
    if cond:
        passed += 1
    else:
        failed += 1
    return cond


def param(name):
    v = api("GET", f"/processors/{A.instance}/realtime/parameters/LOTUS1/{name}")
    e = v.get("engValue", {})
    for k in ("stringValue", "uint32Value", "sint32Value", "floatValue", "doubleValue",
              "booleanValue", "uint64Value", "sint64Value"):
        if k in e:
            return e[k]
    return None


def command(name, args=None, wait_for="CommandComplete_Status", timeout=15):
    r = api("POST", f"/processors/{A.instance}/realtime/commands/LOTUS1/{name}",
            {"args": args or {}})
    cid = r["id"]
    return cid, wait(cid, wait_for, timeout)


def history(cid):
    for c in api("GET", f"/archive/{A.instance}/commands?limit=20").get("commands", []):
        if c["id"] == cid:
            return {a["name"]: next((v for k, v in a["value"].items() if k != "type"), None)
                    for a in c.get("attr", [])}
    return {}


def wait(cid, key, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        h = history(cid)
        if key in h and h[key] not in ("PENDING",):
            return h
        time.sleep(0.5)
    return history(cid)


def accept(cid):
    """The second operator: Commanding > Queues > supervised > Accept."""
    api("POST", f"/processors/{A.instance}/realtime/queues/supervised/commands/{cid}:accept", {})


def events_since(t0):
    evs = api("GET", f"/archive/{A.instance}/events?limit=100").get("events", [])
    return [e for e in evs if e.get("generationTime", "") >= t0]


def main():
    links = {l["name"]: l for l in api("GET", f"/links/{A.instance}")["links"]}
    check(links["tm_realtime"]["status"] == "OK" and int(links["tm_realtime"]["dataInCount"]) > 0,
          f"TM link OK, {links['tm_realtime']['dataInCount']} packets in")
    check(links["tc_realtime"]["status"] == "OK", "TC link OK")
    if param("sec_flag_slot1_untrusted") or param("sec_flag_slot2_untrusted"):
        slot = param("sec_active_slot")
        r = api("POST", f"/processors/{A.instance}/realtime/commands/LOTUS1/RECOVERY_SET_COUNTER",
                {"args": {"slot": f"SLOT_{slot}", "floor": 0}})
        accept(r["id"])
        h = wait(r["id"], "CommandComplete_Status", 15)
        check(h.get("CommandComplete_Status") == "OK",
              "FIRST CONTACT from Yamcs: RECOVERY_SET_COUNTER floor 0 (security unit picks "
              "the safe floor), released from the supervised queue")
        time.sleep(1.5)
    mode = param("mode")
    check(mode in ("NOMINAL", "DEGRADED", "SAFE"), f"/LOTUS1/mode = {mode} (HK SID 1 decoded)")
    lab = secunit.SecurityUnit(None).lab
    kcv = param("sec_slot1_kcv")
    check(param("sec_slot1_state") in ("ACTIVE", "LOADED"), "/LOTUS1/sec_slot1_state decoded")
    check(kcv is not None, f"/LOTUS1/sec_slot1_kcv = {int(kcv):08X} (provisioned key "
                           f"{secunit.kcv(bytes.fromhex(lab['provisioned_key'])):08X})")
    t0 = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(time.time() - 1))

    cid, h = command("PING")
    check(h.get("Verifier_Accepted_Status") == "OK" and h.get("Verifier_Complete_Status") == "OK",
          "PING from Yamcs: Accepted OK, Complete OK (authenticated by the bridge)")
    target = "DEGRADED" if mode == "NOMINAL" else "NOMINAL"
    cid, h = command("SET_MODE", {"mode": target})
    check(h.get("Verifier_PUS_Completed_Status") == "OK" and
          h.get("Verifier_Complete_Status") == "OK",
          f"SET_MODE {target}: TM(1,7) then Complete when HK shows the mode")
    check(param("mode") == target, f"/LOTUS1/mode = {target}")
    cid, h = command("SET_MODE", {"mode": "NOMINAL"})
    check(h.get("CommandComplete_Status") == "OK", "SET_MODE NOMINAL complete")
    cid, h = command("MONITOR_ENABLE", {"param": "T_BAT"})
    check(h.get("CommandComplete_Status") == "OK", "MONITOR_ENABLE T_BAT complete")
    time.sleep(2)
    evs = events_since(t0)
    msgs = [e.get("message", "") for e in evs]
    check(any(m.startswith(f"MODE NOMINAL -> {target}") for m in msgs) or target == "NOMINAL",
          "event log: MODE_CHANGED decoded with its template")

    if A.rekey:
        slot = 2 if param("sec_active_slot") == 1 else 1
        r = api("POST", f"/processors/{A.instance}/realtime/commands/LOTUS1/LOAD_KEY",
                {"args": {"slot": f"SLOT_{slot}"}})
        cid = r["id"]
        check(r.get("queue") == "supervised", "LOAD_KEY (critical) waits in the supervised queue")
        released = False
        for entry in api("GET", f"/processors/{A.instance}/realtime/queues/supervised/commands")\
                .get("commands", []):
            if entry["id"] == cid:
                accept(cid)
                released = True
        check(released, "second operator releases it")
        h = wait(cid, "CommandComplete_Status", 15)
        check(h.get("CommandComplete_Status") == "OK", f"LOAD_KEY slot {slot} complete")
        time.sleep(2)
        check(param(f"sec_slot{slot}_state") == "LOADED", f"slot {slot} LOADED")
        r = api("POST", f"/processors/{A.instance}/realtime/commands/LOTUS1/ACTIVATE_KEY",
                {"args": {"slot": f"SLOT_{slot}"}})
        accept(r["id"])
        h = wait(r["id"], "CommandComplete_Status", 15)
        check(h.get("CommandComplete_Status") == "OK", f"ACTIVATE_KEY slot {slot} complete")
        time.sleep(3)
        check(param("sec_active_slot") == slot, f"/LOTUS1/sec_active_slot = {slot}")
        cid, h = command("PING")
        check(h.get("CommandComplete_Status") == "OK", "PING under the new key, from Yamcs")

    print(f"# result: {passed} passed, {failed} failed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    A = P.parse_args()
    try:
        main()
    except urllib.error.URLError as exc:
        raise SystemExit(f"Yamcs not reachable at {A.url}: {exc}")
