"""LOTUS-1 ground codec: telecommand builders, housekeeping and event
decoders. Builds on pus.py (Session 4), which does the Space Packet, PUS
header and CRC work. Identifiers: src/app/mission.h."""

import os
import struct
import sys

import pus  # noqa: E402  (Session 4)
from pus import PacketError, parse_tm, build_tc  # noqa: E402,F401

APID_CMD, APID_HK, APID_EVENT = 0x010, 0x020, 0x021

PARAM = {1: "V_BUS", 2: "T_BAT", 3: "T_OBC"}
PARAM_ID = {v: k for k, v in PARAM.items()}
TASK = {0: "ACS_CTRL", 1: "TM_HK", 2: "PL_LOG"}
TASK_ID = {v: k for k, v in TASK.items()}
MODE = {0: "NOMINAL", 1: "DEGRADED", 2: "SAFE"}
MODE_ID = {v: k for k, v in MODE.items()}
CAUSE = {0: "BOOT", 1: "GROUND", 2: "MONITOR", 3: "LIVENESS", 4: "WATCHDOG_RESET"}
MON_STATE = {0: "OK", 1: "WARN", 2: "ALARM"}
SIDE = {0: "-", 1: "HIGH", 2: "LOW"}
RESET = {0: "none (cold)", 1: "WATCHDOG"}

EVT_MON = {0x0210: 1, 0x0220: 2, 0x0230: 3}
EVT_MISSED, EVT_RESTARTED, EVT_ESCALATED = 0x0301, 0x0302, 0x0303
EVT_ACTION_INHIBITED, EVT_RECOVERED = 0x0304, 0x0305
EVT_MODE, EVT_INHIBIT, EVT_SCHED_SUSPENDED = 0x0401, 0x0402, 0x0403
EVT_BOOT, EVT_WDG = 0x0501, 0x0502
EVT_MODE_DISCARDED, EVT_MODE_INVALID = 0x0404, 0x0405
EVT_THERM_ATTEMPT, EVT_THERM_RECOVERED, EVT_THERM_EXHAUSTED = 0x0410, 0x0411, 0x0412
EVT_PAYLOAD = 0x0420

ST_INHIBITED, ST_PAYLOAD, ST_SCHED, ST_RX, ST_SUN, ST_WDG = 1, 2, 4, 8, 16, 32
ST_PAYLOAD_ENABLED, ST_THERMAL_RECOVERY = 64, 128

UNITS = {1: (1.0, "mV"), 2: (100.0, "C"), 3: (100.0, "C")}


# ---- Telecommand application data ---------------------------------------------

def tc_set_mode(mode):
    return 8, 1, struct.pack(">BB", 1, MODE_ID[mode] if isinstance(mode, str) else mode)


def tc_payload(on):
    """TC(8,1) function 2: payload on (1) or off (0). Refused (TM(1,8))
    outside NOMINAL."""
    return 8, 1, struct.pack(">BB", 2, 1 if on else 0)


def tc_sched_enable():
    return 11, 1, b""


def tc_sched_disable():
    return 11, 2, b""


def tc_sched_reset():
    return 11, 3, b""


def tc_sched_insert(release_s, packet):
    return 11, 4, struct.pack(">BI", 1, release_s) + packet


def tc_monitor(enable, *params):
    ids = [PARAM_ID[p] if isinstance(p, str) else p for p in params]
    return 12, 1 if enable else 2, struct.pack(">B", len(ids)) + b"".join(
        struct.pack(">H", i) for i in ids)


def tc_fdir_enable():
    return 19, 8, b""


def tc_fdir_inhibit(seconds):
    return 19, 9, struct.pack(">H", seconds)


def tc_inject_param(param, value, samples=0):
    pid = PARAM_ID[param] if isinstance(param, str) else param
    return 200, 1, struct.pack(">HiH", pid, value, samples)


def tc_inject_task_hang(task, times):
    tid = TASK_ID[task] if isinstance(task, str) else task
    return 200, 2, struct.pack(">BB", tid, times)


def tc_inject_supervisor_hang():
    return 200, 3, b""


def tc_inject_clear():
    return 200, 4, b""


# ---- Telemetry decoders ----------------------------------------------------------

def decode_hk(tm):
    d = tm.data
    if not d:
        raise PacketError("empty HK report")
    if d[0] == 1 and len(d) == 37:
        (sid, vbus, tbat, tobc, mode, src, detail, st, en, states, filtered,
         r0, r1, r2, failed, boots, last_reset, resets, escs, tc_acc, tc_rej,
         tm_drop, uptime, inh_left) = struct.unpack(
            ">BHhhBBHBBBHBBBBHBBBHHHIH", d)
        return dict(sid=sid, vbus=vbus, tbat=tbat / 100, tobc=tobc / 100,
                    mode=mode, cause_src=src, cause_detail=detail, status=st,
                    mon_enabled=en,
                    mon_states=[(states >> (2 * i)) & 3 for i in range(3)],
                    filtered=filtered, restarts=[r0, r1, r2], failed=failed,
                    boot_count=boots, last_reset=last_reset,
                    reset_count=resets, escalations=escs, tc_acc=tc_acc,
                    tc_rej=tc_rej, tm_drop=tm_drop, uptime=uptime,
                    inhibit_left=inh_left)
    if d[0] == 2 and len(d) == 17:
        (sid, vbus, tbat, mode, src, detail, st, boots, last_reset,
         uptime) = struct.unpack(">BHhBBHBHBI", d)
        return dict(sid=sid, vbus=vbus, tbat=tbat / 100, mode=mode,
                    cause_src=src, cause_detail=detail, status=st,
                    boot_count=boots, last_reset=last_reset, uptime=uptime)
    raise PacketError(f"unknown HK structure {d[0]} / {len(d)} octets")


def decode_event(tm):
    d = tm.data
    eid = struct.unpack(">H", d[:2])[0]
    e = {"id": eid}
    if eid in EVT_MON and len(d) == 14:
        _, param, state, side, value, limit = struct.unpack(">HHBBii", d)
        e.update(param=param, state=state, side=side, value=value, limit=limit)
    elif eid in (EVT_MISSED, EVT_RESTARTED, EVT_ESCALATED, EVT_RECOVERED):
        _, task, value = struct.unpack(">HBB", d)
        e.update(task=task, value=value)
    elif eid == EVT_ACTION_INHIBITED:
        _, kind, subject = struct.unpack(">HBH", d)
        e.update(kind=kind, subject=subject)
    elif eid == EVT_MODE:
        _, frm, to, src, detail = struct.unpack(">HBBBH", d)
        e.update(frm=frm, to=to, src=src, detail=detail)
    elif eid == EVT_INHIBIT:
        _, on, dur, reason = struct.unpack(">HBHB", d)
        e.update(on=on, duration=dur, reason=reason)
    elif eid == EVT_SCHED_SUSPENDED:
        e.update(entries=d[2])
    elif eid == EVT_BOOT:
        _, boots, last, resets, escs, esc_task, mode, valid = struct.unpack(
            ">HHBBBBBB", d)
        e.update(boot_count=boots, last_reset=last, reset_count=resets,
                 escalations=escs, last_esc_task=esc_task, mode=mode,
                 nv_valid=valid)
    elif eid == EVT_WDG:
        e.update(since_kick_ms=struct.unpack(">I", d[2:6])[0])
    elif eid == EVT_THERM_ATTEMPT:
        _, n, mx, param, value = struct.unpack(">HBBHi", d)
        e.update(attempt=n, max=mx, param=param, value=value)
    elif eid == EVT_THERM_RECOVERED:
        e.update(attempts=d[2])
    elif eid == EVT_THERM_EXHAUSTED:
        _, n, param = struct.unpack(">HBH", d)
        e.update(attempts=n, param=param)
    elif eid == EVT_PAYLOAD:
        e.update(commanded=d[2], effective=d[3])
    elif eid == EVT_MODE_DISCARDED:
        e.update(sm_event=d[2], src=d[3])
    return e


def _fmt_value(param, v):
    scale, unit = UNITS.get(param, (1.0, ""))
    return f"{v / scale:.2f} {unit}" if scale != 1.0 else f"{v} {unit}"


def describe(tm):
    head = (f"APID 0x{tm.apid:03X} seq {tm.seq_count:5d} "
            f"t={tm.time:9.3f}s TM({tm.service},{tm.subtype})")
    try:
        if tm.service == 5:
            e = decode_event(tm)
            i = e["id"]
            if i in EVT_MON:
                text = (f"MONITOR {PARAM[e['param']]} -> "
                        f"{MON_STATE[e['state']]} {SIDE[e['side']]}, value "
                        f"{_fmt_value(e['param'], e['value'])}")
            elif i == EVT_MISSED:
                text = f"HEARTBEAT MISSED {TASK[e['task']]} (miss #{e['value']})"
            elif i == EVT_RESTARTED:
                text = f"TASK RESTARTED {TASK[e['task']]} ({e['value']} in window)"
            elif i == EVT_ESCALATED:
                text = f"TASK ESCALATED {TASK[e['task']]} after {e['value']} restarts"
            elif i == EVT_RECOVERED:
                text = f"TASK RECOVERED {TASK[e['task']]}"
            elif i == EVT_ACTION_INHIBITED:
                what = "restart" if e["kind"] == 1 else "mode change"
                text = f"FDIR ACTION INHIBITED ({what}, subject {e['subject']})"
            elif i == EVT_MODE:
                text = (f"MODE {MODE[e['frm']]} -> {MODE[e['to']]}, cause "
                        f"{CAUSE.get(e['src'], '?')} {e['detail']}")
            elif i == EVT_INHIBIT:
                text = (f"FDIR INHIBIT {'ON for ' + str(e['duration']) + ' s' if e['on'] else 'OFF'}"
                        f" ({'ground' if e['reason'] == 1 else 'timeout'})")
            elif i == EVT_SCHED_SUSPENDED:
                text = f"SCHEDULE SUSPENDED ({e['entries']} activities kept)"
            elif i == EVT_BOOT:
                text = (f"BOOT #{e['boot_count']}: last reset "
                        f"{RESET.get(e['last_reset'], '?')}, resets "
                        f"{e['reset_count']}, escalations {e['escalations']}, "
                        f"mode {MODE[e['mode']]}, NV {'valid' if e['nv_valid'] else 'blank'}")
            elif i == EVT_WDG:
                text = f"WATCHDOG EARLY WARNING ({e['since_kick_ms']} ms since kick)"
            elif i == EVT_THERM_ATTEMPT:
                text = (f"THERMAL RECOVERY attempt {e['attempt']}/{e['max']}, "
                        f"{PARAM[e['param']]} {_fmt_value(e['param'], e['value'])}")
            elif i == EVT_THERM_RECOVERED:
                text = f"THERMAL RECOVERED after {e['attempts']} attempt(s)"
            elif i == EVT_THERM_EXHAUSTED:
                text = (f"THERMAL RECOVERY EXHAUSTED ({e['attempts']} attempts, "
                        f"{PARAM[e['param']]}): SAFE requested")
            elif i == EVT_PAYLOAD:
                text = (f"PAYLOAD {'ENABLED' if e['commanded'] else 'DISABLED'} by ground, "
                        f"{'running' if e['effective'] else 'not running'}")
            elif i == EVT_MODE_DISCARDED:
                text = f"MODE COMMAND DISCARDED (event {e['sm_event']}, source {e['src']})"
            else:
                text = f"event 0x{i:04X} {tm.data.hex().upper()}"
        elif tm.kind == (3, 25):
            h = decode_hk(tm)
            if h["sid"] == 1:
                text = (f"HK1 {MODE[h['mode']]} ({CAUSE[h['cause_src']]} "
                        f"{h['cause_detail']}) st 0x{h['status']:02X} Vbus "
                        f"{h['vbus']} Tbat {h['tbat']:.2f} Tobc {h['tobc']:.2f} "
                        f"mon {'/'.join(MON_STATE[s] for s in h['mon_states'])} "
                        f"en 0x{h['mon_enabled']:X} filt {h['filtered']} "
                        f"rst {h['restarts']} fail 0x{h['failed']:X} boot "
                        f"{h['boot_count']} up {h['uptime']}"
                        + (f" INHIBIT {h['inhibit_left']} s" if h["status"] & ST_INHIBITED else ""))
            else:
                text = (f"HK2 ESSENTIAL {MODE[h['mode']]} ({CAUSE[h['cause_src']]}) "
                        f"st 0x{h['status']:02X} Vbus {h['vbus']} Tbat "
                        f"{h['tbat']:.2f} boot {h['boot_count']} last reset "
                        f"{RESET.get(h['last_reset'], '?')} up {h['uptime']}")
        else:
            return pus.describe(tm)
    except (struct.error, KeyError, PacketError) as exc:
        text = f"undecodable ({exc}): {tm.data.hex().upper()}"
    return f"{head}  {text}"
