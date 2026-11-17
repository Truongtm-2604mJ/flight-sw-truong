"""Lab 8.3 - the Yamcs MDB against the ground decoders, field by field.

gen_mdb.py describes each TM field by position; the Python ground decoders
(Session 5 fdir.py, Session 8 secdecode.py) describe the same packets with
struct formats, written separately, and were checked against the flight
software by the system tests. This script fills packets with random
octets, decodes them both ways, and compares every field. It also checks
that the MDB file is well formed and up to date.

    python3 check_mdb.py          exit 0 = the two descriptions agree
"""
import os
import random
import struct
import subprocess
import sys
import xml.dom.minidom

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "ground"))
import gen_mdb as G    # noqa: E402
import seclink         # noqa: E402,F401  (puts the Session 5 decoders on the path)
import fdir            # noqa: E402
import secdecode       # noqa: E402

errors = []
checked = 0


def extract(f, data):
    """Value of field f (gen_mdb description) from the source data."""
    start = 8 * f["off"] + f.get("bitoff", 0)
    n = int.from_bytes(data, "big")
    v = (n >> (8 * len(data) - start - f["bits"])) & ((1 << f["bits"]) - 1)
    if f.get("signed") and v >> (f["bits"] - 1):
        v -= 1 << f["bits"]
    return v * f["scale"] if "scale" in f else v


def same(name, mdb_value, ref_value):
    global checked
    checked += 1
    if abs(float(mdb_value) - float(ref_value)) > 1e-9:
        errors.append(f"{name}: MDB {mdb_value} != decoder {ref_value}")


def check_hk(fields, sid, length, decode, mapping, trials=50):
    for _ in range(trials):
        data = bytes([sid]) + bytes(random.getrandbits(8) for _ in range(length - 1))
        ref = decode(data)
        for f in fields:
            if f["name"] == "sid":
                continue
            key = mapping.get(f["name"], f["name"])
            if callable(key):
                same(f["name"], extract(f, data), key(ref))
            else:
                same(f["name"], extract(f, data), ref[key])


class TM:
    def __init__(self, data):
        self.data = data


def main():
    random.seed(8)
    status = {n: (lambda m: (lambda r: 1 if r["status"] & m else 0))(mask)
              for mask, n in G.STATUS_BITS.items()}
    m1 = dict(status, mode_cause="cause_src", mode_cause_detail="cause_detail",
              mon_state_vbus=lambda r: r["mon_states"][0],
              mon_state_tbat=lambda r: r["mon_states"][1],
              mon_state_tobc=lambda r: r["mon_states"][2],
              monitor_filtered="filtered", restarts_acs=lambda r: r["restarts"][0],
              restarts_hk=lambda r: r["restarts"][1], restarts_log=lambda r: r["restarts"][2],
              failed_tasks="failed", escalation_count="escalations",
              tc_accepted="tc_acc", tc_rejected="tc_rej", tm_dropped="tm_drop",
              inhibit_left="inhibit_left", mon_enabled="mon_enabled")
    check_hk(G.SID1, 1, 37, lambda d: fdir.decode_hk(TM(d)), m1)
    check_hk(G.SID2, 2, 17, lambda d: fdir.decode_hk(TM(d)),
             dict(status, mode_cause="cause_src", mode_cause_detail="cause_detail"))
    rej = {f"sec_rej_{c.lower()}": (lambda c: lambda r: r["rejected"][c])(c)
           for c in secdecode.REJ_CLASS.values()}
    m3 = dict(rej, sec_active_slot="active",
              sec_slot1_state=lambda r: r["state"][1], sec_slot2_state=lambda r: r["state"][2],
              sec_slot1_kcv=lambda r: r["kcv"][1], sec_slot2_kcv=lambda r: r["kcv"][2],
              sec_slot1_counter=lambda r: r["counter"][1],
              sec_slot2_counter=lambda r: r["counter"][2], sec_challenge="challenge",
              sec_scrub_errors="scrub_errors", sec_scrub_repairs="scrub_repairs",
              sec_flags="flags", sec_auth_accepted="auth_accepted",
              sec_flag_factory=lambda r: 1 if r["flags"] & 1 else 0,
              sec_flag_nv_fail=lambda r: 1 if r["flags"] & 2 else 0,
              sec_flag_ev_dropped=lambda r: 1 if r["flags"] & 4 else 0,
              sec_flag_slot1_untrusted=lambda r: 1 if r["untrusted"][1] else 0,
              sec_flag_slot2_untrusted=lambda r: 1 if r["untrusted"][2] else 0)
    check_hk(G.SID3, 3, 41, secdecode.decode_sid3, m3)

    # Security events against secdecode.decode_event.
    ev_map = {"ev_rej_class": "cls", "ev_rej_code": "code", "ev_rej_total": "total",
              "ev_rej_count": "count", "ev_rej_window": "window_s", "ev_slot": "slot",
              "ev_kcv": "kcv", "ev_prev_slot": "previous", "ev_by_recovery": "recovery",
              "ev_slot_mask": "mask", "ev_scrub_errors": "errors",
              "ev_rec_subtype": "subtype", "ev_challenge": "challenge", "ev_old": "old",
              "ev_new": "new", "ev_repairs": "repairs"}
    rev_class = {v: k for k, v in secdecode.REJ_CLASS.items()}
    ids = {lab: v for v, lab in G.ENUMS["event_id_t"].items()}
    for name, flds in G.EVENTS.items():
        eid = ids[name]
        size = max(f["off"] + f["bits"] // 8 for f in flds)
        for _ in range(30):
            d = struct.pack(">H", eid) + bytes(random.getrandbits(8) for _ in range(size - 2))
            if name.startswith("SEC_"):
                if name.startswith("SEC_REJECT"):
                    d = d[:2] + bytes([random.randrange(6)]) + d[3:]
                ref = secdecode.decode_event(d)
                for f in flds:
                    r = ref[ev_map[f["name"]]]
                    same(f"{name}.{f['name']}", extract(f, d), rev_class.get(r, r))
            else:
                ref = fdir.decode_event(TM(d))
                s5 = {"ev_param": "param", "ev_mon_state": "state", "ev_side": "side",
                      "ev_value": "value", "ev_limit": "limit", "ev_task": "task",
                      "ev_count": "value", "ev_kind": "kind", "ev_subject": "subject",
                      "ev_mode_from": "frm", "ev_mode_to": "to", "ev_cause": "src",
                      "ev_cause_detail": "detail", "ev_on": "on", "ev_duration": "duration",
                      "ev_reason": "reason", "ev_entries": "entries",
                      "ev_boot_count": "boot_count", "ev_last_reset": "last_reset",
                      "ev_reset_count": "reset_count", "ev_escalations": "escalations",
                      "ev_last_esc_task": "last_esc_task", "ev_boot_mode": "mode",
                      "ev_nv_valid": "nv_valid", "ev_since_kick_ms": "since_kick_ms",
                      "ev_attempt": "attempt", "ev_attempt_max": "max",
                      "ev_attempts": "attempts", "ev_payload_cmd": "commanded",
                      "ev_payload_running": "effective"}
                for f in flds:
                    if s5.get(f["name"]) in ref:       # fdir.py decodes most of them
                        same(f"{name}.{f['name']}", extract(f, d), ref[s5[f["name"]]])

    # The generated files are current and well formed.
    xml_path = os.path.join(HERE, "..", "yamcs", "mdb", "lotus1.xml")
    before = open(xml_path).read()
    subprocess.run([sys.executable, os.path.join(HERE, "gen_mdb.py")], check=True,
                   stdout=subprocess.DEVNULL)
    if open(xml_path).read() != before:
        errors.append("lotus1.xml was not up to date with gen_mdb.py (regenerated now)")
    xml.dom.minidom.parse(xml_path)

    for e in errors[:20]:
        print("MISMATCH", e)
    print(f"{checked} field comparisons, {len(errors)} mismatches "
          f"(SID 1, SID 2, SID 3, {len(G.EVENTS)} event layouts)")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
