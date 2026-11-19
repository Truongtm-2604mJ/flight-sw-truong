"""Lab 8.3 - execute a Yamcs command stack (.ycs) through the REST API.

The same file the operators run in the web interface (Commanding > Stacks),
executed step by step by a script: proves the procedure works before it is
used, and gives a log. Critical commands go through the supervised queue;
--release-supervised stands for the second operator.

    python3 run_procedure.py ../procedures/SAFE-01-recover-from-safe.ycs \
        [--setup-safe] [--release-supervised] [--log run.log]

--setup-safe first puts the spacecraft in SAFE (test build: V_BUS forced to
25.0 V, below the 26.0 V low limit, which the FDIR answers with SAFE).
"""
import argparse
import json
import sys
import time

import yamcs_check as yc   # the REST helpers


def log(msg):
    line = time.strftime("%H:%M:%S ") + msg
    print(line, flush=True)
    if LOG:
        LOG.write(line + "\n")
        LOG.flush()


def value(p):
    return yc.param(p.split("/")[-1])


def eq(a, b):
    return str(a).lower() == str(b).lower()


def run_command(step, release):
    name = step["name"].split("/")[-1]
    args = {a["name"]: a["value"] for a in step.get("arguments", [])}
    ack = step.get("advancement", {}).get("acknowledgment", "Verifier_Complete")
    r = yc.api("POST", f"/processors/{yc.A.instance}/realtime/commands/LOTUS1/{name}",
               {"args": args})
    cid = r["id"]
    if r.get("queue") == "supervised":
        if not release:
            log(f"   {name} waits in the supervised queue: release it, then rerun")
            return False
        yc.accept(cid)
        log(f"   {name}: released from the supervised queue (second operator)")
    h = yc.wait(cid, ack + "_Status", 20)
    stages = {k[:-7]: v for k, v in h.items() if k.endswith("_Status") and
              (k.startswith("Verifier_") or k == "CommandComplete_Status")}
    log(f"   {name} {args or ''}: " + ", ".join(f"{k}={v}" for k, v in stages.items()))
    return h.get(ack + "_Status") == "OK"


def test(op, v, want):
    """The operators of the Yamcs stack 'verify' step."""
    if op in ("eq", "neq"):
        return eq(v, want) == (op == "eq")
    try:
        a, b = float(v), float(want)
    except (TypeError, ValueError):
        return False
    return {"lt": a < b, "lte": a <= b, "gt": a > b, "gte": a >= b}[op]


def run_verify(step):
    deadline = time.time() + step.get("timeout", 10000) / 1000
    time.sleep(step.get("delay", 0) / 1000)
    while True:
        vals = [(c, value(c["parameter"])) for c in step["condition"]]
        ok = all(test(c["operator"], v, c["value"]) for c, v in vals)
        if ok or time.time() > deadline:
            log("   " + ", ".join(f"{c['parameter']}={v} (want {c['value']})"
                                  for c, v in vals) + ("  OK" if ok else "  NOT MET"))
            return ok
        time.sleep(0.5)


def main():
    global LOG
    ap = argparse.ArgumentParser()
    ap.add_argument("stack")
    ap.add_argument("--setup-safe", action="store_true")
    ap.add_argument("--release-supervised", action="store_true")
    ap.add_argument("--log")
    a = ap.parse_args()
    LOG = open(a.log, "w") if a.log else None
    steps = json.load(open(a.stack))["steps"]

    if a.setup_safe:
        log("SETUP: TEST_INJECT_PARAM V_BUS 25000 mV (test build) to enter SAFE")
        yc.api("POST", f"/processors/{yc.A.instance}/realtime/commands/LOTUS1/TEST_INJECT_PARAM",
               {"args": {"param": "V_BUS", "value": 25000, "samples": 0}})
        t0 = time.time()
        while value("mode") != "SAFE" and time.time() - t0 < 20:
            time.sleep(0.5)
        log(f"SETUP: mode = {value('mode')}, cause {value('mode_cause')}")

    for i, s in enumerate(steps):
        if s["type"] == "text":
            log(f"[{i}] " + s["text"].splitlines()[0])
            continue
        log(f"[{i}] {s['type'].upper()}: {s.get('comment', '')}")
        if s["type"] == "check":
            for p in s["parameters"]:
                log(f"   {p['parameter']} = {value(p['parameter'])}")
            ok = True
        elif s["type"] == "command":
            ok = run_command(s, a.release_supervised)
        else:
            ok = run_verify(s)
        if not ok:
            log(f"PROCEDURE STOPPED at step {i}")
            sys.exit(1)
    log("PROCEDURE COMPLETE")


LOG = None
if __name__ == "__main__":
    main()
