"""Session 8 ground decoders: HK SID 3 (key state) and security events
(APID 0x022). Layout: session-08/app/sec_app.c build_sid3()."""

import struct

APID_SEC = 0x022
SLOT_STATE = {0: "EMPTY", 1: "LOADED", 2: "ACTIVE", 3: "CORRUPT"}
REJ_CLASS = {0: "STRUCTURE", 1: "CRC", 2: "AUTH", 3: "REPLAY", 4: "WINDOW",
             5: "STATE"}
EVT = {0x0601: "REJECT_FIRST", 0x0602: "REJECT_SUMMARY", 0x0603: "KEY_LOADED",
       0x0604: "KEY_ACTIVATED", 0x0605: "KEY_CORRUPT", 0x0606: "COUNTER_VOTE",
       0x0607: "RECOVERY_USED", 0x0608: "KEY_ERASED", 0x0609: "COUNTER_SET",
       0x060A: "KEY_LOAD_FAILED", 0x060B: "FACTORY_STATE", 0x060C: "KEY_REPAIRED"}
EVT_ID = {v: k for k, v in EVT.items()}
FAIL = {10: "AUTH", 11: "REPLAY", 12: "WINDOW", 13: "SEC_STATE"}
FL_FACTORY, FL_NV_FAIL, FL_EV_DROPPED = 0x01, 0x02, 0x04
FL_UNTRUSTED = {1: 0x08, 2: 0x10}       # slot n waits for its first contact


def decode_sid3(d):
    if len(d) != 41 or d[0] != 3:
        raise ValueError(f"not HK SID 3 ({len(d)} octets)")
    (sid, active, st1, st2, kcv1, kcv2, c1, c2, chal) = struct.unpack(">BBBBIIIII", d[:24])
    rej = struct.unpack(">6H", d[24:36])
    return dict(sid=sid, active=active, state={1: st1, 2: st2},
                kcv={1: kcv1, 2: kcv2}, counter={1: c1, 2: c2}, challenge=chal,
                rejected={REJ_CLASS[i]: v for i, v in enumerate(rej)},
                scrub_errors=d[36], scrub_repairs=d[37], flags=d[38],
                untrusted={n: bool(d[38] & m) for n, m in FL_UNTRUSTED.items()},
                auth_accepted=struct.unpack(">H", d[39:41])[0])


def decode_event(d):
    eid = struct.unpack(">H", d[:2])[0]
    e = {"id": eid, "name": EVT.get(eid, f"0x{eid:04X}")}
    if eid == 0x0601:
        e.update(cls=REJ_CLASS.get(d[2], d[2]), code=d[3],
                 total=struct.unpack(">I", d[4:8])[0])
    elif eid == 0x0602:
        cnt, tot, win = struct.unpack(">IIH", d[4:14])
        e.update(cls=REJ_CLASS.get(d[2], d[2]), code=d[3], count=cnt, total=tot,
                 window_s=win)
    elif eid == 0x0603:
        e.update(slot=d[2], kcv=struct.unpack(">I", d[3:7])[0])
    elif eid == 0x0604:
        e.update(slot=d[2], previous=d[3], kcv=struct.unpack(">I", d[4:8])[0],
                 recovery=d[8])
    elif eid == 0x0605:
        e.update(mask=d[2], errors=struct.unpack(">H", d[3:5])[0])
    elif eid == 0x0606:
        e.update(mask=d[2])
    elif eid == 0x0607:
        e.update(subtype=d[2], slot=d[3], challenge=struct.unpack(">I", d[4:8])[0])
    elif eid in (0x0608, 0x060A):
        e.update(slot=d[2])
    elif eid == 0x0609:
        old, new = struct.unpack(">II", d[3:11])
        e.update(slot=d[2], old=old, new=new)
    elif eid == 0x060C:
        e.update(repairs=struct.unpack(">H", d[2:4])[0])
    return e


def describe(tm):
    head = (f"APID 0x{tm.apid:03X} seq {tm.seq_count:5d} "
            f"t={tm.time:9.3f}s TM({tm.service},{tm.subtype})")
    try:
        if tm.kind == (3, 25):
            h = decode_sid3(tm.data)
            rej = " ".join(f"{k}={v}" for k, v in h["rejected"].items() if v)
            return (f"{head} SECURITY HK: active slot {h['active']}, slot1 "
                    f"{SLOT_STATE[h['state'][1]]} kcv {h['kcv'][1]:08X} ctr "
                    f"{h['counter'][1]}, slot2 {SLOT_STATE[h['state'][2]]} kcv "
                    f"{h['kcv'][2]:08X} ctr {h['counter'][2]}, challenge "
                    f"{h['challenge']}, rejected [{rej or 'none'}], scrub "
                    f"err {h['scrub_errors']} rep {h['scrub_repairs']}, "
                    f"flags 0x{h['flags']:02X}"
                    + (" FIRST CONTACT NEEDED" if h["untrusted"][h["active"]] else ""))
        if tm.service == 5:
            e = decode_event(tm.data)
            extra = ", ".join(f"{k}={v}" for k, v in e.items() if k not in ("id", "name"))
            return f"{head} SECURITY EVENT {e['name']} {extra}"
    except (ValueError, KeyError, struct.error) as exc:
        return f"{head} undecodable security TM: {exc}"
    return head
