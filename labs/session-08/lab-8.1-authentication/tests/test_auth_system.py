"""Session 8 system tests, Labs 8.1 and 8.2, against the test build on the
host simulation (or SIS with --pty). Every command goes through the ground
security unit (ground/secunit.py, Python hmac / cryptography), so these
tests also show that the ground and onboard implementations agree.

    python3 test_auth_system.py --spawn ../../app/lab8-test.host [--log f]

Lab 8.1 cases are tagged [8.1-n], Lab 8.2 cases [8.2-n].
"""

import argparse
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "ground"))

import seclink    # noqa: E402
import secdecode  # noqa: E402
import secunit    # noqa: E402
import fdir       # noqa: E402
from link import Checker  # noqa: E402

PING = (17, 1, b"")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    seclink.SecureLink.add_arguments(p)
    p.set_defaults(nv_file="/tmp/s8_system_nv.bin")
    args = p.parse_args()
    for f in (args.nv_file, args.nv_file + ".sec", args.nv_file + ".ground.json"):
        if os.path.exists(f):
            os.remove(f)

    link = seclink.SecureLink(args)
    su = link.su
    c = Checker(link)

    def reports_for(tc, since):
        rid = tc[:4]
        return [t for t in link.telemetry[since:]
                if t.service == 1 and t.data[:4] == rid]

    def command(cmd, note, timeout=3.0, **kw):
        """Send; return the list of TM(1,x) subtypes received for it."""
        since = len(link.telemetry)
        tc = link.send(cmd, note, **kw)
        link.wait_for(lambda t: t.service == 1 and t.data[:4] == tc[:4] and
                      t.subtype in (2, 7, 8), timeout)
        return [t.subtype for t in reports_for(tc, since)], tc

    def silent(tc_or_send, wait=1.6):
        """No TM(1,x) at all for this request within `wait` s."""
        since = len(link.telemetry)
        tc = tc_or_send() if callable(tc_or_send) else tc_or_send
        if not callable(tc_or_send):
            link.send_raw(tc, "replayed as received")
        link.collect(wait)
        return not reports_for(tc, since), tc

    def sid3(timeout=3.0):
        return link.latest_sid3(timeout)

    def mode_now():
        tm = link.wait_for(lambda t: t.apid == fdir.APID_HK and t.kind == (3, 25), 3.0)
        return fdir.decode_hk(tm)["mode"] if tm else None

    def events(since, name):
        return link.sec_events(since, name)

    link.collect(2.5)
    h = sid3()
    lab = su.lab
    c.check(h and h["active"] == 1 and h["state"][1] == 2 and h["state"][2] == 0,
            "boot: slot 1 ACTIVE, slot 2 EMPTY (factory key store)")
    c.check(h and h["kcv"][1] == secunit.kcv(bytes.fromhex(lab["provisioned_key"])),
            f"boot: onboard KCV of slot 1 = ground KCV "
            f"{secunit.kcv(bytes.fromhex(lab['provisioned_key'])):08X}")
    c.check(h and h["flags"] & 1, "boot: SID 3 flags FACTORY")
    c.check(events(0, "FACTORY_STATE"), "boot: FACTORY_STATE event")

    # ---- [8.2-0] first contact: a factory key store trusts no counter ------
    c.check(h and h["untrusted"][1], "[8.2-0] factory state: slot 1 counter UNTRUSTED")
    ok, _ = silent(lambda: link.send(PING, "before first contact", seq=1000))
    link.collect(1.2)
    h = sid3()
    c.check(ok and h["rejected"]["STATE"] == 1,
            "[8.2-0] before first contact, a valid command is refused (STATE): NV loss "
            "cannot re-open commands recorded under the provisioned key")
    c.check(link.first_contact(), "[8.2-0] first contact: TC(210,10) with the recovery "
                                  "key sets the floor, TM(1,7)")
    link.collect(1.2)
    h = sid3()
    c.check(h and not h["untrusted"][1] and h["counter"][1] == su.next_seq(1) - 1,
            f"[8.2-0] slot 1 trusted, counter at the ground floor {h and h['counter'][1]}")

    # ---- [8.1-1] a valid command executes and advances the counter --------
    seq = su.next_seq(1)
    subs, _ = command(PING, "valid")
    c.check(subs == [1, 7] and any(t.kind == (17, 2) for t in link.telemetry[-6:]),
            "[8.1-1] valid TC(17,1): TM(1,1), TM(17,2), TM(1,7)")
    link.collect(1.2)
    h = sid3()
    c.check(h and h["counter"][1] == seq, f"[8.1-1] SID 3 counter slot 1 = {seq}")

    subs, _ = command(fdir.tc_set_mode("DEGRADED"), "mode DEGRADED")
    c.check(subs == [1, 7], "[8.1-1] authenticated TC(8,1) DEGRADED accepted")
    link.collect(2.5)
    c.check(mode_now() == 1, "[8.1-1] mode is DEGRADED")
    command(fdir.tc_set_mode("NOMINAL"), "mode NOMINAL")
    link.collect(2.5)

    # ---- [8.1-2] wrong key: rejected at authentication, never executes ------
    wrong = bytes(32)
    since = len(link.telemetry)
    ok, _ = silent(lambda: link.send(fdir.tc_set_mode("SAFE"), "WRONG KEY",
                                     key=wrong))
    c.check(ok, "[8.1-2] TC(8,1) SAFE under a wrong key: no TM(1,x) at all")
    link.collect(2.5)
    c.check(mode_now() == 0, "[8.1-2] mode still NOMINAL: never executed")
    first = events(since, "REJECT_FIRST")
    c.check(first and first[0]["cls"] == "AUTH" and first[0]["code"] == 10,
            "[8.1-2] REJECT_FIRST event, class AUTH, code 10")
    h = sid3()
    c.check(h and h["rejected"]["AUTH"] == 1 and h["counter"][1] == su.next_seq(1) - 2,
            "[8.1-2] SID 3: AUTH rejections 1, counter unchanged")

    # ---- [8.1-3] replay -------------------------------------------------------
    subs, captured = command(PING, "to be replayed")
    c.check(subs == [1, 7], "[8.1-3] original accepted")
    ok, _ = silent(captured)
    c.check(ok, "[8.1-3] the same packet again: no answer")
    link.collect(1.2)
    h = sid3()
    c.check(h and h["rejected"]["REPLAY"] == 1, "[8.1-3] SID 3: REPLAY rejections 1")

    # ---- [8.1-4] far future counter: WINDOW policy ------------------------------
    h = sid3()
    last = h["counter"][1]
    ok, _ = silent(lambda: link.send(PING, "counter + 65537",
                                     seq=last + secunit.WINDOW + 1))
    c.check(ok, "[8.1-4] counter 65537 ahead: rejected, no answer")
    link.collect(1.2)
    h = sid3()
    c.check(h and h["rejected"]["WINDOW"] == 1 and h["counter"][1] == last,
            "[8.1-4] SID 3: WINDOW rejections 1, counter not moved")
    subs, _ = command(PING, "next in sequence")
    c.check(subs == [1, 7], "[8.1-4] the next in-sequence command still works")

    # ---- [8.1-5] a burst of invalid commands: bounded events -------------------
    link.collect(12.0)                         # let the open windows close
    since = len(link.telemetry)
    n_burst = 400
    for i in range(n_burst):
        kind = i % 4
        if kind == 0:
            link.send(PING, "", key=wrong)                     # AUTH
        elif kind == 1:
            link.send_raw(captured)                            # REPLAY
        elif kind == 2:
            link.send_plain(PING)                              # STRUCTURE
        else:
            pkt = bytearray(su.protect(fdir.build_tc(link.next_tc_seq(), 17, 1),
                                       seq=1))
            pkt[-1] ^= 1
            link.send_raw(bytes(pkt))                          # CRC
    t_burst = time.monotonic()
    link.collect(22.0)
    evs = [e for e in link.sec_events(since) if e["name"].startswith("REJECT")]
    per_class = {}
    for e in evs:
        per_class.setdefault(e["cls"], []).append(e["name"])
    c.check(len(evs) <= 2 * 4 + 4,
            f"[8.1-5] {n_burst} invalid TCs -> {len(evs)} rejection events "
            f"({ {k: len(v) for k, v in per_class.items()} })")
    c.check(all(len(v) <= 3 and v[0] == "REJECT_FIRST" for v in per_class.values()),
            "[8.1-5] per class: one REJECT_FIRST, then summaries only")
    summed = {}
    for e in evs:
        summed[e["cls"]] = summed.get(e["cls"], 0) + (1 if e["name"] == "REJECT_FIRST"
                                                      else e["count"])
    c.check(all(v == n_burst // 4 for v in summed.values()) and len(summed) == 4,
            f"[8.1-5] nothing lost: FIRST + summary counts = {n_burst // 4} per class")
    c.check(not [t for t in link.telemetry[since:] if t.service == 1],
            "[8.1-5] no TM(1,x) for any of them")
    subs, _ = command(PING, "after the burst")
    c.check(subs == [1, 7], "[8.1-5] commanding unaffected after the burst")
    _ = t_burst

    # ---- [8.1-6] counter survives a reset ---------------------------------------
    h = sid3()
    before = h["counter"][1]
    link.restart(keep_nv=True)
    after_reset = len(link.telemetry)
    link.collect(2.5)
    link.first_contact()      # no-op on the host (NV kept); SIS has no NV
    h = sid3()
    c.check(h and h["counter"][1] == before and not h["flags"] & 1,
            f"[8.1-6] after reset: counter {before} restored from NV, not factory")
    ok, _ = silent(captured)
    c.check(ok, "[8.1-6] pre-reset packet replayed after the reset: rejected")
    subs, _ = command(PING, "after reset")
    c.check(subs == [1, 7], "[8.1-6] next command accepted after reset")

    # ---- scheduled commands: authenticated at uplink, trusted at release ----
    hk = link.wait_for(lambda t: t.apid == fdir.APID_HK and t.kind == (3, 25), 3.0)
    t_now = fdir.decode_hk(hk)["uptime"]
    inner = fdir.build_tc(link.next_tc_seq(), 17, 1, source_id=0x77)
    command(fdir.tc_sched_enable(), "schedule on")
    subs, _ = command(fdir.tc_sched_insert(t_now + 3, inner), "insert TC(17,1)")
    got = link.wait_for(lambda t: t.kind == (17, 2) and t.dest_id == 0x77, 8.0)
    c.check(subs == [1, 7] and got is not None,
            "schedule: TC(11,4) authenticated, inner TC released and executed")

    # ---- [8.2-1] rekey: load (wrapped), check KCV, then activate ---------------
    since = len(link.telemetry)
    svc, sub, data, newkey = su.tc_load_key(2)
    subs, _ = command((svc, sub, data), "LOAD_KEY slot 2")
    link.collect(1.2)
    h = sid3()
    c.check(subs == [1, 7] and h["state"][2] == 1 and h["active"] == 1,
            "[8.2-1] TC(210,1): slot 2 LOADED, slot 1 still ACTIVE")
    c.check(h["kcv"][2] == secunit.kcv(newkey),
            f"[8.2-1] onboard KCV {h['kcv'][2]:08X} = ground KCV of the new key")
    c.check(events(since, "KEY_LOADED"), "[8.2-1] KEY_LOADED event")

    svc, sub, data, _bad = su.tc_load_key(2, corrupt=True)
    since = len(link.telemetry)
    subs, _ = command((svc, sub, data), "LOAD_KEY slot 2, damaged in transit")
    link.collect(1.2)
    h = sid3()
    c.check(8 in subs and h["kcv"][2] == secunit.kcv(newkey) and
            events(since, "KEY_LOAD_FAILED"),
            "[8.2-1] damaged wrapped key: TM(1,8), KEY_LOAD_FAILED, slot 2 unchanged")

    subs, _ = command(su.tc_activate(2), "ACTIVATE slot 2")
    link.collect(1.2)
    h = sid3()
    ok = subs == [1, 7] and h["active"] == 2 and h["state"][1] == 1
    c.check(ok, "[8.2-2] TC(210,2): slot 2 ACTIVE, slot 1 LOADED (fallback)")
    if ok:
        su.activated(2)
    ok, _ = silent(lambda: link.send(PING, "old key", spi=1))
    c.check(ok, "[8.2-2] a command under the old key is now refused")
    subs, _ = command(PING, "new key")
    c.check(subs == [1, 7], "[8.2-2] a command under the new key, counter 1, accepted")

    # ---- [8.2-3] key state in HK, never key material --------------------------
    secrets = [bytes.fromhex(lab["provisioned_key"]), newkey, su.recovery_key, su.kek]
    leaked = [t for t in link.telemetry for k in secrets
              for i in range(0, len(k) - 7) if k[i:i + 8] in t.raw]
    c.check(not leaked, f"[8.2-3] no 8-octet run of any key in {len(link.telemetry)} "
                        f"TM packets")

    # ---- [8.2-4] counter corrupted high -> recovery resets it -----------------
    command((211, 1, struct.pack(">BIII", 2, 0xFFFFFF00, 0xFFFFFF00, 0xFFFFFF00)),
            "INJECT counter slot 2 corrupted high")
    ok, _ = silent(lambda: link.send(PING, "blocked by the high counter"))
    c.check(ok, "[8.2-4] counter 0xFFFFFF00: every command is a replay")
    h = sid3()
    chal = h["challenge"]
    floor = su.safe_floor(2)
    since = len(link.telemetry)
    subs, rec = command(su.tc_recovery_counter(2, floor), "RECOVERY counter slot 2",
                        spi=0, seq=chal)
    link.collect(1.2)
    h = sid3()
    c.check(subs == [1, 7] and h["counter"][2] == floor and h["challenge"] == chal + 1,
            f"[8.2-4] TC(210,10) under the recovery key, challenge {chal}: counter "
            f"set to {floor}, challenge now {chal + 1}")
    c.check(events(since, "RECOVERY_USED") and events(since, "COUNTER_SET"),
            "[8.2-4] RECOVERY_USED and COUNTER_SET events")
    su.set_next_seq(2, floor + 1)
    subs, _ = command(PING, "after recovery")
    c.check(subs == [1, 7], "[8.2-4] normal commanding restored")
    ok, _ = silent(rec)
    c.check(ok, "[8.2-4] the recovery command replayed: rejected")
    ok, _ = silent(lambda: link.send(fdir.tc_set_mode("SAFE"), "recovery key, TC(8,1)",
                                     spi=0, seq=sid3()["challenge"]))
    link.collect(2.5)
    c.check(ok and mode_now() == 0, "[8.2-4] the recovery key cannot command a mode")

    # ---- [8.2-5] memory upsets in counters and keys ----------------------------
    v = su.next_seq(2) - 1
    command((211, 1, struct.pack(">BIII", 2, v, v ^ 0x00800000, v)),
            "INJECT one counter copy flipped")
    subs, _ = command(PING, "after single counter upset")
    c.check(subs == [1, 7], "[8.2-5] one counter copy upset: voted out, still accepted")
    since = len(link.telemetry)
    command((211, 2, bytes([2, 0x02, 7, 0x20])), "INJECT key slot 2, one copy")
    link.collect(2.5)
    c.check(events(since, "KEY_REPAIRED"), "[8.2-5] one key copy upset: KEY_REPAIRED")
    subs, _ = command(PING, "after key repair")
    c.check(subs == [1, 7], "[8.2-5] active key still works after the repair")

    since = len(link.telemetry)
    command((211, 2, bytes([2, 0x05, 3, 0x01])), "INJECT key slot 2, same bit in 2 copies")
    link.collect(2.5)
    h = sid3()
    c.check(events(since, "KEY_CORRUPT") and h["state"][2] == 3,
            "[8.2-5] same bit in two copies: KEY_CORRUPT, slot 2 CORRUPT")
    ok, _ = silent(lambda: link.send(PING, "active key lost"))
    c.check(ok, "[8.2-5] commands under the lost key refused")

    # ---- [8.2-6] recovery: activate the spare --------------------------------
    since = len(link.telemetry)
    subs, _ = command(su.tc_recovery_activate(1), "RECOVERY activate slot 1",
                      spi=0, seq=sid3()["challenge"])
    link.collect(1.2)
    h = sid3()
    ok = subs == [1, 7] and h["active"] == 1
    c.check(ok, "[8.2-6] TC(210,11) under the recovery key: slot 1 ACTIVE again")
    if ok:
        su.activated(1)
    ev = events(since, "KEY_ACTIVATED")
    c.check(ev and ev[0]["recovery"] == 1, "[8.2-6] KEY_ACTIVATED, recovery flag set")
    subs, _ = command(PING, "fallback key")
    c.check(subs == [1, 7], "[8.2-6] commanding restored on the fallback key")
    subs, _ = command(su.tc_erase(2), "ERASE corrupt slot 2")
    link.collect(1.2)
    h = sid3()
    c.check(subs == [1, 7] and h["state"][2] == 0, "[8.2-6] TC(210,3): slot 2 EMPTY")

    c.check_sequences(after_reset)    # counters restart at every boot
    c.finish()


if __name__ == "__main__":
    main()
