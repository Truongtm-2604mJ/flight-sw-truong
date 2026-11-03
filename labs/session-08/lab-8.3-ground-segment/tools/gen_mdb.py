"""Lab 8.3 - generate the Yamcs mission database (XTCE) from one table of
what the flight software actually sends and accepts.

Every position here is taken from the C that builds or parses the packet,
named next to each table (fdir6.c hk_full(), sec_app.c build_sid3(), ...).
check_mdb.py then compares the generated layout with the independent
Python ground decoders, field by field, so the two descriptions of the
interface cannot drift apart silently.

    python3 gen_mdb.py        -> ../yamcs/mdb/s8.xml, ../yamcs/etc/events.json,
                                 ../docs/mdb.md
"""
import json
import os
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
SS = "S8"
TM_DATA = 6 + 13                  # primary header + PUS-C TM secondary header

APID_CMD, APID_HK, APID_EVT, APID_SEC = 0x010, 0x020, 0x021, 0x022

# ---- Types ------------------------------------------------------------------------
# name: (kind, bits, extra)
ENUMS = {
    "mode_t": {0: "NOMINAL", 1: "DEGRADED", 2: "SAFE"},
    "cause_t": {0: "BOOT", 1: "GROUND", 2: "MONITOR", 3: "LIVENESS", 4: "WATCHDOG_RESET"},
    "reset_t": {0: "NONE_COLD", 1: "WATCHDOG"},
    "mon_t": {0: "OK", 1: "WARN", 2: "ALARM"},
    "mon2_t": {0: "OK", 1: "WARN", 2: "ALARM"},     # the same, 2 bit encoding
    "slot_state_t": {0: "EMPTY", 1: "LOADED", 2: "ACTIVE", 3: "CORRUPT"},
    "rej_class_t": {0: "STRUCTURE", 1: "CRC", 2: "AUTH", 3: "REPLAY", 4: "WINDOW",
                    5: "STATE"},
    "fail_t": {1: "SP_VERSION", 2: "LENGTH", 3: "CHECKSUM", 4: "PACKET_HEADER",
               5: "APID", 6: "PUS_VERSION", 7: "SERVICE", 8: "SUBTYPE",
               9: "APP_DATA", 10: "AUTH", 11: "REPLAY", 12: "WINDOW",
               13: "SEC_STATE", 16: "EXECUTION", 255: "NO_REPORT"},
    "param_t": {1: "V_BUS", 2: "T_BAT", 3: "T_OBC"},
    "task_t": {0: "ACS_CTRL", 1: "TM_HK", 2: "PL_LOG"},
    "side_t": {0: "NONE", 1: "HIGH", 2: "LOW"},
    "event_id_t": {
        0x0210: "MON_VBUS", 0x0220: "MON_TBAT", 0x0230: "MON_TOBC",
        0x0301: "HEARTBEAT_MISSED", 0x0302: "TASK_RESTARTED", 0x0303: "TASK_ESCALATED",
        0x0304: "ACTION_INHIBITED", 0x0305: "TASK_RECOVERED",
        0x0401: "MODE_CHANGED", 0x0402: "INHIBIT_CHANGED", 0x0403: "SCHED_SUSPENDED",
        0x0404: "MODE_CMD_DISCARDED", 0x0405: "MODE_INVALID",
        0x0501: "BOOT_REPORT", 0x0502: "WDG_WARNING",
        0x0601: "SEC_REJECT_FIRST", 0x0602: "SEC_REJECT_SUMMARY",
        0x0603: "SEC_KEY_LOADED", 0x0604: "SEC_KEY_ACTIVATED", 0x0605: "SEC_KEY_CORRUPT",
        0x0606: "SEC_COUNTER_VOTE", 0x0607: "SEC_RECOVERY_USED", 0x0608: "SEC_KEY_ERASED",
        0x0609: "SEC_COUNTER_SET", 0x060A: "SEC_KEY_LOAD_FAILED",
        0x060B: "SEC_FACTORY_STATE", 0x060C: "SEC_KEY_REPAIRED"},
}
ENUM_ALARMS = {"mode_t": {"DEGRADED": "warning", "SAFE": "critical"},
               "slot_state_t": {"CORRUPT": "critical"},
               "mon2_t": {"WARN": "warning", "ALARM": "critical"}}


def ptype(p):
    """Parameter type name for a field description."""
    if "enum" in p:
        return p["enum"]
    if p.get("bool"):
        return "flag_t"
    if "scale" in p:
        return f"{p['name']}_t"
    return f"{'i' if p.get('signed') else 'u'}{p['bits']}"


# ---- Telemetry ---------------------------------------------------------------------
# Each field: name, octet offset in the source data (or bit offset "bit"),
# bits, plus signed / enum / scale+unit / bool / description.

def F(name, off, bits, **kw):
    d = dict(name=name, off=off, bits=bits)
    d.update(kw)
    return d


# fdir6.c hk_full(), HK_SID1_LEN 37
SID1 = [F("sid", 0, 8), F("vbus", 1, 16, unit="mV", descr="bus voltage"),
        F("tbat", 3, 16, signed=True, scale=0.01, unit="degC", warn=43, crit=45,
          descr="battery temperature"),
        F("tobc", 5, 16, signed=True, scale=0.01, unit="degC", descr="OBC temperature"),
        F("mode", 7, 8, enum="mode_t", descr="vehicle mode"),
        F("mode_cause", 8, 8, enum="cause_t"), F("mode_cause_detail", 9, 16),
        F("status", 11, 8, descr="status bits, see the flags"),
        F("mon_enabled", 12, 8, descr="bit i: monitor i enabled"),
        F("mon_state_vbus", 13, 2, bitoff=6, enum="mon2_t"),
        F("mon_state_tbat", 13, 2, bitoff=4, enum="mon2_t"),
        F("mon_state_tobc", 13, 2, bitoff=2, enum="mon2_t"),
        F("monitor_filtered", 14, 16), F("restarts_acs", 16, 8), F("restarts_hk", 17, 8),
        F("restarts_log", 18, 8), F("failed_tasks", 19, 8), F("boot_count", 20, 16),
        F("last_reset", 22, 8, enum="reset_t"), F("reset_count", 23, 8),
        F("escalation_count", 24, 8), F("tc_accepted", 25, 16), F("tc_rejected", 27, 16),
        F("tm_dropped", 29, 16), F("uptime", 31, 32, unit="s"),
        F("inhibit_left", 35, 16, unit="s")]
# status bits (fdir_app.h ST_*): bit mask -> name
STATUS_BITS = {0x01: "fdir_inhibited", 0x02: "payload_on", 0x04: "sched_enabled",
               0x08: "rx_on", 0x10: "sun_pointing", 0x20: "wdg_warning"}
for mask, n in STATUS_BITS.items():
    SID1.append(F(n, 11, 1, bitoff=7 - (mask.bit_length() - 1), bool=True))

# fdir6.c hk_essential(), HK_SID2_LEN 17
SID2 = [F("sid", 0, 8), F("vbus", 1, 16), F("tbat", 3, 16, signed=True, scale=0.01),
        F("mode", 5, 8, enum="mode_t"), F("mode_cause", 6, 8, enum="cause_t"),
        F("mode_cause_detail", 7, 16), F("status", 9, 8), F("boot_count", 10, 16),
        F("last_reset", 12, 8, enum="reset_t"), F("uptime", 13, 32)]
for mask, n in STATUS_BITS.items():
    SID2.append(F(n, 9, 1, bitoff=7 - (mask.bit_length() - 1), bool=True))

# sec_app.c build_sid3(), HK_SID3_LEN 41: key STATE, never key material
SID3 = [F("sid", 0, 8), F("sec_active_slot", 1, 8),
        F("sec_slot1_state", 2, 8, enum="slot_state_t"),
        F("sec_slot2_state", 3, 8, enum="slot_state_t"),
        F("sec_slot1_kcv", 4, 32, descr="key check value: HMAC(key,'KCV')[0..3]"),
        F("sec_slot2_kcv", 8, 32), F("sec_slot1_counter", 12, 32),
        F("sec_slot2_counter", 16, 32),
        F("sec_challenge", 20, 32, descr="SEQ the next recovery command must carry"),
        F("sec_rej_structure", 24, 16), F("sec_rej_crc", 26, 16),
        F("sec_rej_auth", 28, 16), F("sec_rej_replay", 30, 16),
        F("sec_rej_window", 32, 16), F("sec_rej_state", 34, 16),
        F("sec_scrub_errors", 36, 8), F("sec_scrub_repairs", 37, 8),
        F("sec_flags", 38, 8), F("sec_auth_accepted", 39, 16),
        F("sec_flag_factory", 38, 1, bitoff=7, bool=True),
        F("sec_flag_nv_fail", 38, 1, bitoff=6, bool=True),
        F("sec_flag_ev_dropped", 38, 1, bitoff=5, bool=True),
        F("sec_flag_slot1_untrusted", 38, 1, bitoff=4, bool=True,
          descr="slot 1 counter untrusted: FIRST CONTACT needed (RECOVERY_SET_COUNTER)"),
        F("sec_flag_slot2_untrusted", 38, 1, bitoff=3, bool=True)]

# Events: aux data after the 2 octet event ID (fdir6.c, sec_service.c,
# sec_report.c, sec_app.c). Offsets are from the start of the aux data.
EVENTS = {
    "MON_VBUS": [F("ev_param", 2, 16, enum="param_t"), F("ev_mon_state", 4, 8, enum="mon_t"),
                 F("ev_side", 5, 8, enum="side_t"), F("ev_value", 6, 32, signed=True),
                 F("ev_limit", 10, 32, signed=True)],
    "HEARTBEAT_MISSED": [F("ev_task", 2, 8, enum="task_t"), F("ev_count", 3, 8)],
    "ACTION_INHIBITED": [F("ev_kind", 2, 8), F("ev_subject", 3, 16)],
    "MODE_CHANGED": [F("ev_mode_from", 2, 8, enum="mode_t"), F("ev_mode_to", 3, 8, enum="mode_t"),
                     F("ev_cause", 4, 8, enum="cause_t"), F("ev_cause_detail", 5, 16)],
    "INHIBIT_CHANGED": [F("ev_on", 2, 8), F("ev_duration", 3, 16), F("ev_reason", 5, 8)],
    "SCHED_SUSPENDED": [F("ev_entries", 2, 8)],
    "MODE_CMD_DISCARDED": [F("ev_sm_event", 2, 8), F("ev_src", 3, 8)],
    "MODE_INVALID": [F("ev_raw_state", 2, 32)],
    "BOOT_REPORT": [F("ev_boot_count", 2, 16), F("ev_last_reset", 4, 8, enum="reset_t"),
                    F("ev_reset_count", 5, 8), F("ev_escalations", 6, 8),
                    F("ev_last_esc_task", 7, 8), F("ev_boot_mode", 8, 8, enum="mode_t"),
                    F("ev_nv_valid", 9, 8)],
    "WDG_WARNING": [F("ev_since_kick_ms", 2, 32)],
    "SEC_REJECT_FIRST": [F("ev_rej_class", 2, 8, enum="rej_class_t"),
                         F("ev_rej_code", 3, 8, enum="fail_t"), F("ev_rej_total", 4, 32)],
    "SEC_REJECT_SUMMARY": [F("ev_rej_class", 2, 8, enum="rej_class_t"),
                           F("ev_rej_code", 3, 8, enum="fail_t"), F("ev_rej_count", 4, 32),
                           F("ev_rej_total", 8, 32), F("ev_rej_window", 12, 16)],
    "SEC_KEY_LOADED": [F("ev_slot", 2, 8), F("ev_kcv", 3, 32)],
    "SEC_KEY_ACTIVATED": [F("ev_slot", 2, 8), F("ev_prev_slot", 3, 8), F("ev_kcv", 4, 32),
                          F("ev_by_recovery", 8, 8)],
    "SEC_KEY_CORRUPT": [F("ev_slot_mask", 2, 8), F("ev_scrub_errors", 3, 16)],
    "SEC_COUNTER_VOTE": [F("ev_slot_mask", 2, 8)],
    "SEC_RECOVERY_USED": [F("ev_rec_subtype", 2, 8), F("ev_slot", 3, 8),
                          F("ev_challenge", 4, 32)],
    "SEC_KEY_ERASED": [F("ev_slot", 2, 8)],
    "SEC_COUNTER_SET": [F("ev_slot", 2, 8), F("ev_old", 3, 32), F("ev_new", 7, 32)],
    "SEC_KEY_LOAD_FAILED": [F("ev_slot", 2, 8)],
    "SEC_KEY_REPAIRED": [F("ev_repairs", 2, 16)],
}
for alias in ("MON_TBAT", "MON_TOBC"):
    EVENTS[alias] = EVENTS["MON_VBUS"]
for alias in ("TASK_RESTARTED", "TASK_ESCALATED", "TASK_RECOVERED"):
    EVENTS[alias] = EVENTS["HEARTBEAT_MISSED"]

TEMPLATES = {
    "MON_VBUS": "V_BUS monitor {ev_mon_state} {ev_side}: {ev_value} mV (limit {ev_limit})",
    "MON_TBAT": "T_BAT monitor {ev_mon_state} {ev_side}: {ev_value} cdegC (limit {ev_limit})",
    "MON_TOBC": "T_OBC monitor {ev_mon_state} {ev_side}: {ev_value} cdegC (limit {ev_limit})",
    "HEARTBEAT_MISSED": "Heartbeat missed: {ev_task} (miss {ev_count})",
    "TASK_RESTARTED": "Task restarted: {ev_task} ({ev_count} in window)",
    "TASK_ESCALATED": "Task escalated: {ev_task} after {ev_count} restarts",
    "TASK_RECOVERED": "Task recovered: {ev_task}",
    "ACTION_INHIBITED": "FDIR action inhibited (kind {ev_kind}, subject {ev_subject})",
    "MODE_CHANGED": "MODE {ev_mode_from} -> {ev_mode_to} (cause {ev_cause} {ev_cause_detail})",
    "INHIBIT_CHANGED": "FDIR inhibit on={ev_on} for {ev_duration} s (reason {ev_reason})",
    "SCHED_SUSPENDED": "Schedule suspended ({ev_entries} activities kept)",
    "MODE_CMD_DISCARDED": "Mode command discarded (event {ev_sm_event}, source {ev_src})",
    "MODE_INVALID": "Mode state invalid ({ev_raw_state}), recovered to SAFE",
    "BOOT_REPORT": "BOOT #{ev_boot_count}: last reset {ev_last_reset}, mode {ev_boot_mode}, "
                   "NV valid {ev_nv_valid}",
    "WDG_WARNING": "Watchdog early warning ({ev_since_kick_ms} ms since kick)",
    "SEC_REJECT_FIRST": "SECURITY: TC rejected, class {ev_rej_class} ({ev_rej_code}), "
                        "total {ev_rej_total}",
    "SEC_REJECT_SUMMARY": "SECURITY: {ev_rej_count} more {ev_rej_class} rejections in "
                          "{ev_rej_window} s, total {ev_rej_total}",
    "SEC_KEY_LOADED": "SECURITY: key loaded in slot {ev_slot}, KCV {ev_kcv; %08X}",
    "SEC_KEY_ACTIVATED": "SECURITY: slot {ev_slot} ACTIVE (was {ev_prev_slot}), KCV "
                         "{ev_kcv; %08X}, by recovery {ev_by_recovery}",
    "SEC_KEY_CORRUPT": "SECURITY: key LOST in slot mask {ev_slot_mask} "
                       "(scrub errors {ev_scrub_errors})",
    "SEC_COUNTER_VOTE": "SECURITY: counter copies disagreed (mask {ev_slot_mask}), "
                        "largest taken",
    "SEC_RECOVERY_USED": "SECURITY: RECOVERY command {ev_rec_subtype} slot {ev_slot}, "
                         "challenge {ev_challenge}",
    "SEC_KEY_ERASED": "SECURITY: slot {ev_slot} erased",
    "SEC_COUNTER_SET": "SECURITY: slot {ev_slot} counter {ev_old} -> {ev_new}",
    "SEC_KEY_LOAD_FAILED": "SECURITY: wrapped key for slot {ev_slot} did not verify",
    "SEC_FACTORY_STATE": "SECURITY: key store blank or invalid, factory state",
    "SEC_KEY_REPAIRED": "SECURITY: key store upset repaired by the scrub ({ev_repairs})",
}

# ---- Commands -------------------------------------------------------------------------
# name: (service, subtype, [args (name, type, bits)], fixed prefix octets,
#        significance or None, description)
# Argument types: u8 u16 u32 i32 or an enum name from ARG_ENUMS.
ARG_ENUMS = {"mode_arg": {0: "NOMINAL", 1: "DEGRADED", 2: "SAFE"},
             "slot_arg": {1: "SLOT_1", 2: "SLOT_2"},
             "param_arg": {1: "V_BUS", 2: "T_BAT", 3: "T_OBC"},
             "task_arg": {0: "ACS_CTRL", 1: "TM_HK", 2: "PL_LOG"}}
CRIT = "critical"
COMMANDS = {
    "PING": (17, 1, [], b"", None, "TC(17,1) are-you-alive"),
    "SET_MODE": (8, 1, [("mode", "mode_arg", 8)], b"\x01", "warning",
                 "TC(8,1) function 1: request a vehicle mode (executed at the next 2 s "
                 "supervisor cycle: wait for Mode_Reached, not only Complete, NCR-01)"),
    "SCHED_ENABLE": (11, 1, [], b"", None, "TC(11,1) enable the time based schedule"),
    "SCHED_DISABLE": (11, 2, [], b"", None, "TC(11,2) disable the schedule"),
    "SCHED_RESET": (11, 3, [], b"", "warning", "TC(11,3) delete every scheduled activity"),
    "MONITOR_ENABLE": (12, 1, [("param", "param_arg", 16)], b"\x01", None,
                       "TC(12,1) enable one parameter monitor"),
    "MONITOR_DISABLE": (12, 2, [("param", "param_arg", 16)], b"\x01", "warning",
                        "TC(12,2) disable one parameter monitor"),
    "FDIR_ENABLE": (19, 8, [], b"", None, "TC(19,8) FDIR actions on"),
    "FDIR_INHIBIT": (19, 9, [("seconds", "u16", 16)], b"", "warning",
                     "TC(19,9) inhibit FDIR actions for a time"),
    "LOAD_KEY": (210, 1, [("slot", "slot_arg", 8)], b"", CRIT,
                 "TC(210,1) load a new key into an INACTIVE slot. The ground security "
                 "unit generates the key and wraps it (AES-128-CCM); key material never "
                 "enters the MCS. Compare SID 3 KCV with the bridge log before ACTIVATE."),
    "ACTIVATE_KEY": (210, 2, [("slot", "slot_arg", 8)], b"", CRIT,
                     "TC(210,2) make a LOADED slot the active key"),
    "ERASE_KEY": (210, 3, [("slot", "slot_arg", 8)], b"", CRIT,
                  "TC(210,3) zeroise an inactive slot"),
    "RECOVERY_SET_COUNTER": (210, 10, [("slot", "slot_arg", 8), ("floor", "u32", 32)], b"",
                             CRIT, "TC(210,10) RECOVERY KEY: set a slot's anti-replay "
                             "counter. floor 0 = the security unit's safe floor (above "
                             "every value ever used)."),
    "RECOVERY_ACTIVATE": (210, 11, [("slot", "slot_arg", 8)], b"", CRIT,
                          "TC(210,11) RECOVERY KEY: activate a LOADED spare slot when "
                          "the active key is lost"),
    "TEST_INJECT_PARAM": (200, 1, [("param", "param_arg", 16), ("value", "i32", 32),
                                   ("samples", "u16", 16)], b"", "warning",
                          "TEST BUILD ONLY: TC(200,1) force a sensor value"),
    "TEST_INJECT_CLEAR": (200, 4, [], b"", None, "TEST BUILD ONLY: TC(200,4) clear injections"),
    "TEST_SEC_COUNTER": (211, 1, [("slot", "slot_arg", 8), ("copy0", "u32", 32),
                                  ("copy1", "u32", 32), ("copy2", "u32", 32)], b"", CRIT,
                         "TEST BUILD ONLY: TC(211,1) overwrite counter copies (upset)"),
    "TEST_SEC_KEY_FLIP": (211, 2, [("slot", "slot_arg", 8), ("copies", "u8", 8),
                                   ("octet", "u8", 8), ("mask", "u8", 8)], b"", CRIT,
                          "TEST BUILD ONLY: TC(211,2) flip key bits in the copies set "
                          "in 'copies' (bit 0 = the copy used for MACs)"),
}


# ---- XML writer ---------------------------------------------------------------------------

class X:
    def __init__(self):
        self.lines = []
        self.depth = 0

    def open(self, tag, **attrs):
        self.lines.append("  " * self.depth + f"<{tag}{self._a(attrs)}>")
        self.depth += 1

    def close(self, tag):
        self.depth -= 1
        self.lines.append("  " * self.depth + f"</{tag}>")

    def leaf(self, tag, text=None, **attrs):
        if text is None:
            self.lines.append("  " * self.depth + f"<{tag}{self._a(attrs)}/>")
        else:
            self.lines.append("  " * self.depth + f"<{tag}{self._a(attrs)}>{escape(str(text))}</{tag}>")

    @staticmethod
    def _a(attrs):
        return "".join(f' {k.rstrip("_").replace("__", ":")}="{escape(str(v), {chr(34): "&quot;"})}"'
                       for k, v in attrs.items())


def int_encoding(x, bits, signed):
    x.leaf("IntegerDataEncoding", sizeInBits=bits,
           encoding="twosComplement" if signed else "unsigned")


def all_fields():
    seen = {}
    for lst in [SID1, SID2, SID3] + list(EVENTS.values()):
        for f in lst:
            if f["name"] in seen:
                a, b = seen[f["name"]], f
                if (ptype(a), a["bits"]) != (ptype(b), b["bits"]):
                    raise SystemExit(f"field {f['name']} has two definitions")
            else:
                seen[f["name"]] = f
    return seen


def write_types(x, fields):
    x.open("ParameterTypeSet")
    done = set()
    for bits in (8, 11, 14, 16, 32):
        x.open("IntegerParameterType", name=f"u{bits}", signed="false")
        int_encoding(x, bits, False)
        x.close("IntegerParameterType")
        done.add(f"u{bits}")
    for bits in (16, 32):
        x.open("IntegerParameterType", name=f"i{bits}", signed="true")
        int_encoding(x, bits, True)
        x.close("IntegerParameterType")
        done.add(f"i{bits}")
    x.open("BooleanParameterType", name="flag_t", oneStringValue="ON", zeroStringValue="OFF")
    x.leaf("IntegerDataEncoding", sizeInBits=1)
    x.close("BooleanParameterType")
    enum_bits = {}
    for f in fields.values():
        if "enum" in f:
            if enum_bits.setdefault(f["enum"], f["bits"]) != f["bits"]:
                raise SystemExit(f"enum {f['enum']} used with two sizes")
    enum_bits.setdefault("event_id_t", 16)
    enum_bits.setdefault("fail_t", 8)
    for name, bits in sorted(enum_bits.items()):
        x.open("EnumeratedParameterType", name=name)
        int_encoding(x, bits, False)
        x.open("EnumerationList")
        for v, lab in sorted(ENUMS[name].items()):
            x.leaf("Enumeration", value=v, label=lab)
        x.close("EnumerationList")
        if name in ENUM_ALARMS:
            x.open("DefaultAlarm")
            x.open("EnumerationAlarmList")
            for lab, lvl in ENUM_ALARMS[name].items():
                x.leaf("EnumerationAlarm", alarmLevel=lvl, enumerationLabel=lab)
            x.close("EnumerationAlarmList")
            x.close("DefaultAlarm")
        x.close("EnumeratedParameterType")
    for f in fields.values():
        if "scale" in f:
            x.open("FloatParameterType", name=ptype(f), sizeInBits=64)
            if f.get("unit"):
                x.open("UnitSet")
                x.leaf("Unit", f["unit"])
                x.close("UnitSet")
            x.open("IntegerDataEncoding", sizeInBits=f["bits"],
                   encoding="twosComplement" if f.get("signed") else "unsigned")
            x.open("DefaultCalibrator")
            x.open("PolynomialCalibrator")
            x.leaf("Term", exponent=1, coefficient=f["scale"])
            x.close("PolynomialCalibrator")
            x.close("DefaultCalibrator")
            x.close("IntegerDataEncoding")
            if "warn" in f:
                x.open("DefaultAlarm")
                x.open("StaticAlarmRanges")
                x.leaf("WarningRange", maxInclusive=f["warn"])
                x.leaf("CriticalRange", maxInclusive=f["crit"])
                x.close("StaticAlarmRanges")
                x.close("DefaultAlarm")
            x.close("FloatParameterType")
    for bits in (2,):
        pass
    x.close("ParameterTypeSet")


def entry(x, name, bit):
    x.open("ParameterRefEntry", parameterRef=name)
    x.open("LocationInContainerInBits", referenceLocation="containerStart")
    x.leaf("FixedValue", bit)
    x.close("LocationInContainerInBits")
    x.close("ParameterRefEntry")


def container(x, name, base, fields_at, restrictions, partition=False, descr=None):
    x.open("SequenceContainer", name=name, **({"shortDescription": descr} if descr else {}))
    if partition:
        x.open("AncillaryDataSet")
        x.leaf("AncillaryData", "UseAsArchivingPartition", name="Yamcs", mimeType="text/plain")
        x.close("AncillaryDataSet")
    x.open("EntryList")
    for n, bit in fields_at:
        entry(x, n, bit)
    x.close("EntryList")
    if base:
        x.open("BaseContainer", containerRef=base)
        if restrictions:
            x.open("RestrictionCriteria")
            x.open("ComparisonList")
            for p, op, v in restrictions:
                x.leaf("Comparison", parameterRef=p, comparisonOperator=op, value=v)
            x.close("ComparisonList")
            x.close("RestrictionCriteria")
        x.close("BaseContainer")
    x.close("SequenceContainer")


def at(fields, data_off=TM_DATA):
    return [(f["name"], 8 * (data_off + f["off"]) + f.get("bitoff", 0)) for f in fields]


def tm(x):
    fields = all_fields()
    x.open("TelemetryMetaData")
    write_types(x, fields)
    x.open("ParameterSet")
    hdr = [("apid", "u11", "APID"), ("seqcount", "u14", "source sequence count"),
           ("pus_service", "u8", "PUS service type"), ("pus_subtype", "u8", "PUS subtype"),
           ("pus_msg_counter", "u16", ""), ("pus_destination", "u16", "destination ID"),
           ("obt_coarse", "u32", "onboard time, s since boot"),
           ("obt_fine", "u16", "onboard time, 1/65536 s"),
           ("tc_ack_apid", "u11", "request ID: APID of the TC"),
           ("tc_ack_seq", "u14", "request ID: sequence count of the TC"),
           ("tc_ack_accept_failure", "fail_t", "TM(1,2) failure code"),
           ("tc_ack_complete_failure", "fail_t", "TM(1,8) failure code"),
           ("event_id", "event_id_t", "TM(5,x) event ID")]
    for n, t, d in hdr:
        x.leaf("Parameter", name=n, parameterTypeRef=t, **({"shortDescription": d} if d else {}))
    for f in fields.values():
        if f["name"] == "sid":
            continue
        x.leaf("Parameter", name=f["name"], parameterTypeRef=ptype(f),
               **({"shortDescription": f["descr"]} if f.get("descr") else {}))
    x.leaf("Parameter", name="hk_sid", parameterTypeRef="u8", shortDescription="HK structure ID")
    x.close("ParameterSet")

    x.open("ContainerSet")
    container(x, "ccsds", None, [("apid", 5), ("seqcount", 18)], None)
    container(x, "pus_tm", "ccsds",
              [("pus_service", 56), ("pus_subtype", 64), ("pus_msg_counter", 72),
               ("pus_destination", 88), ("obt_coarse", 104), ("obt_fine", 136)],
              [("apid", "!=", 2047)])
    container(x, "tc_verification", "pus_tm",
              [("tc_ack_apid", 8 * TM_DATA + 5), ("tc_ack_seq", 8 * TM_DATA + 18)],
              [("pus_service", "==", 1)], partition=True, descr="TM(1,x)")
    container(x, "tc_accept_failure", "tc_verification",
              [("tc_ack_accept_failure", 8 * (TM_DATA + 4))], [("pus_subtype", "==", 2)])
    container(x, "tc_complete_failure", "tc_verification",
              [("tc_ack_complete_failure", 8 * (TM_DATA + 4))], [("pus_subtype", "==", 8)])
    container(x, "alive_report", "pus_tm", [], [("pus_service", "==", 17),
                                                ("pus_subtype", "==", 2)], partition=True,
              descr="TM(17,2)")
    container(x, "hk_report", "pus_tm", [("hk_sid", 8 * TM_DATA)],
              [("pus_service", "==", 3), ("pus_subtype", "==", 25)])
    sid = [f for f in SID1 if f["name"] != "sid"]
    container(x, "hk_sid1_full", "hk_report", at(sid),
              [("apid", "==", APID_HK), ("hk_sid", "==", 1)], partition=True,
              descr="HK SID 1, full (fdir6.c hk_full)")
    sid = [f for f in SID2 if f["name"] != "sid"]
    container(x, "hk_sid2_essential", "hk_report", at(sid),
              [("apid", "==", APID_HK), ("hk_sid", "==", 2)], partition=True,
              descr="HK SID 2, essential, SAFE mode (fdir6.c hk_essential)")
    sid = [f for f in SID3 if f["name"] != "sid"]
    container(x, "hk_sid3_security", "hk_report", at(sid),
              [("apid", "==", APID_SEC), ("hk_sid", "==", 3)], partition=True,
              descr="HK SID 3, key state (sec_app.c build_sid3)")
    container(x, "event_report", "pus_tm", [("event_id", 8 * TM_DATA)],
              [("pus_service", "==", 5)], partition=True, descr="TM(5,x)")
    for name, flds in EVENTS.items():
        container(x, f"ev_{name.lower()}", "event_report", at(flds),
                  [("event_id", "==", name)])
    x.close("ContainerSet")

    # Mode_Reached verifier (NCR-01 workaround: TM(1,7) is not the mode change)
    x.close("TelemetryMetaData")


def pus1_verifier(x, tag, nm, stage, param, window):
    """TM(1,x) matched by request ID (APID + sequence count) to this command.
    A TC rejected before authentication gets no TM(1,2): its Accepted stage
    ends in TIMEOUT, and the reason is in the security events."""
    x.open(tag, name=nm)
    x.open("CustomAlgorithm", name=f"verif_{nm.lower()}")
    x.leaf("AlgorithmText",
           f"org.yamcs.pus.Pus1Verifier({{stage: {stage}, template: \"failure code {{errorCode}}\"}})",
           language="Java")
    x.open("InputSet")
    for ref, inp in (("/yamcs/cmd/arg/apid", "sentApid"),
                     ("/yamcs/cmdHist/ccsds-seqcount", "sentSeq"),
                     ("tc_ack_apid", "rcvdApid"), ("tc_ack_seq", "rcvdSeq"),
                     ("pus_subtype", "subtype"), (param, "errorCode")):
        x.leaf("InputParameterInstanceRef", parameterRef=ref, inputName=inp)
    x.close("InputSet")
    x.close("CustomAlgorithm")
    x.leaf("CheckWindow", timeToStopChecking=window, timeWindowIsRelativeTo="commandRelease")
    x.close(tag)


def tc(x):
    x.open("CommandMetaData")
    x.open("ArgumentTypeSet")
    for t, bits, signed in (("u4", 4, False), ("u8", 8, False), ("u11", 11, False),
                            ("u16", 16, False), ("u32", 32, False), ("i32", 32, True)):
        x.open("IntegerArgumentType", name=t, signed="true" if signed else "false")
        int_encoding(x, bits, signed)
        x.close("IntegerArgumentType")
    for name, values in ARG_ENUMS.items():
        bits = 16 if name == "param_arg" else 8
        x.open("EnumeratedArgumentType", name=name)
        int_encoding(x, bits, False)
        x.open("EnumerationList")
        for v, lab in values.items():
            x.leaf("Enumeration", value=v, label=lab)
        x.close("EnumerationList")
        x.close("EnumeratedArgumentType")
    x.close("ArgumentTypeSet")

    x.open("MetaCommandSet")
    # Abstract base: CCSDS + PUS-C TC secondary header; PUS 1 verifiers.
    x.open("MetaCommand", name="s8_tc", abstract="true",
           shortDescription="Session 8 PUS-C telecommand to APID 0x010. The ground "
                            "security unit (bridge) adds SPI, SEQ and MAC.")
    x.open("ArgumentList")
    x.leaf("Argument", name="apid", argumentTypeRef="u11", initialValue=APID_CMD)
    x.leaf("Argument", name="ackflags", argumentTypeRef="u4", initialValue=9)
    x.leaf("Argument", name="service", argumentTypeRef="u8")
    x.leaf("Argument", name="subtype", argumentTypeRef="u8")
    x.leaf("Argument", name="source", argumentTypeRef="u16", initialValue=1)
    x.close("ArgumentList")
    x.open("CommandContainer", name="s8_tc")
    x.open("EntryList")
    for n, v, b in (("ccsds_version", "00", 3), ("ccsds_type_tc", "01", 1),
                    ("ccsds_sec_hdr", "01", 1)):
        x.leaf("FixedValueEntry", name=n, binaryValue=v, sizeInBits=b)
    x.leaf("ArgumentRefEntry", argumentRef="apid")
    x.leaf("FixedValueEntry", name="ccsds_seq_flags", binaryValue="03", sizeInBits=2)
    x.leaf("FixedValueEntry", name="ccsds_seq", binaryValue="0000", sizeInBits=14)
    x.leaf("FixedValueEntry", name="ccsds_length", binaryValue="0000", sizeInBits=16)
    x.leaf("FixedValueEntry", name="pus_version", binaryValue="02", sizeInBits=4)
    for a in ("ackflags", "service", "subtype", "source"):
        x.leaf("ArgumentRefEntry", argumentRef=a)
    x.close("EntryList")
    x.close("CommandContainer")
    x.open("VerifierSet")
    pus1_verifier(x, "AcceptedVerifier", "Accepted", 1, "tc_ack_accept_failure", "PT5S")
    x.close("VerifierSet")     # Complete: per command (a base verifier is not overridable)
    x.close("MetaCommand")

    for name, (svc, sub, args, prefix, sig, descr) in COMMANDS.items():
        x.open("MetaCommand", name=name, shortDescription=descr)
        x.open("BaseMetaCommand", metaCommandRef="s8_tc")
        x.open("ArgumentAssignmentList")
        x.leaf("ArgumentAssignment", argumentName="service", argumentValue=svc)
        x.leaf("ArgumentAssignment", argumentName="subtype", argumentValue=sub)
        x.close("ArgumentAssignmentList")
        x.close("BaseMetaCommand")
        if args:
            x.open("ArgumentList")
            for an, at_, _bits in args:
                x.leaf("Argument", name=an, argumentTypeRef=at_)
            x.close("ArgumentList")
        x.open("CommandContainer", name=name)
        x.open("EntryList")
        for i, b in enumerate(prefix):
            x.leaf("FixedValueEntry", name=f"fixed{i}", binaryValue=f"{b:02X}", sizeInBits=8)
        for an, _t, _b in args:
            x.leaf("ArgumentRefEntry", argumentRef=an)
        x.close("EntryList")
        x.close("CommandContainer")
        if sig:
            x.leaf("DefaultSignificance", consequenceLevel=sig,
                   reasonForWarning=descr.split(":")[0] if sig == CRIT else "changes the vehicle state")
        if name == "SET_MODE":
            # NCR-01: TM(1,7) comes when the request is posted, the mode changes
            # at the next 2 s supervisor cycle. Here TM(1,7) is an execution
            # stage, and the command is Complete only when HK shows the mode.
            x.open("VerifierSet")
            pus1_verifier(x, "ExecutionVerifier", "PUS_Completed", 7,
                          "tc_ack_complete_failure", "PT10S")
            x.open("CompleteVerifier", name="Complete")
            x.open("CustomAlgorithm", name="verif_mode_reached")
            x.open("AncillaryDataSet")
            x.leaf("AncillaryData", "mode", name="Yamcs:AlgorithmMandatoryInput")
            x.close("AncillaryDataSet")
            x.leaf("AlgorithmText",
                   "if (mode.rawValue == target.rawValue) { return true; }",
                   language="JavaScript")
            x.open("InputSet")
            x.leaf("InputParameterInstanceRef", parameterRef="/yamcs/cmd/arg/mode",
                   inputName="target")
            x.leaf("InputParameterInstanceRef", parameterRef="mode", inputName="mode")
            x.close("InputSet")
            x.close("CustomAlgorithm")
            x.leaf("CheckWindow", timeToStopChecking="PT6S",
                   timeWindowIsRelativeTo="commandRelease")
            x.close("CompleteVerifier")
            x.close("VerifierSet")
        else:
            x.open("VerifierSet")
            pus1_verifier(x, "CompleteVerifier", "Complete", 7, "tc_ack_complete_failure",
                          "PT10S")
            x.close("VerifierSet")
        x.close("MetaCommand")
    x.close("MetaCommandSet")
    x.close("CommandMetaData")


def doc():
    out = ["# Session 8 mission database (generated by tools/gen_mdb.py)\n",
           "Positions are octets in the TM source data (after the 6 + 13 octet headers).\n"]
    for title, apid, lst in (("HK SID 1 (APID 0x020)", APID_HK, SID1),
                             ("HK SID 2 (APID 0x020)", APID_HK, SID2),
                             ("HK SID 3, security (APID 0x022)", APID_SEC, SID3)):
        out.append(f"\n## {title}\n\n| Parameter | Octet | Bits | Type |\n|---|---:|---:|---|")
        for f in lst:
            pos = f"{f['off']}" + (f".{f['bitoff']}" if "bitoff" in f else "")
            out.append(f"| {f['name']} | {pos} | {f['bits']} | {ptype(f)} |")
    out.append("\n## Events (TM(5,x), APIDs 0x021 and 0x022)\n\n| ID | Name | Template |"
               "\n|---|---|---|")
    for v, lab in sorted(ENUMS["event_id_t"].items()):
        out.append(f"| 0x{v:04X} | {lab} | {TEMPLATES.get(lab, '')} |")
    out.append("\n## Commands\n\n| Command | TC | Arguments | Significance |\n|---|---|---|---|")
    for n, (s, st, args, pre, sig, d) in COMMANDS.items():
        a = ", ".join(f"{an}:{t}" for an, t, _ in args) or "-"
        out.append(f"| {n} | ({s},{st}) | {a} | {sig or '-'} |")
    return "\n".join(out) + "\n"


def main():
    x = X()
    x.lines.append('<?xml version="1.0" encoding="UTF-8"?>')
    x.lines.append("<!-- GENERATED by lab-8.3-ground-segment/tools/gen_mdb.py. Do not edit. -->")
    x.open("SpaceSystem", name=SS, xmlns="http://www.omg.org/spec/XTCE/20180204",
           xmlns__xsi="http://www.w3.org/2001/XMLSchema-instance")
    x.leaf("Header", validationStatus="Working", version="8.0",
           date="2026-11-02T00:00:00Z")
    tm(x)
    tc(x)
    x.close("SpaceSystem")
    with open(os.path.join(ROOT, "yamcs", "mdb", "s8.xml"), "w") as f:
        f.write("\n".join(x.lines) + "\n")
    ev = [{"eventId": lab, "template": TEMPLATES.get(lab, lab)}
          for lab in ENUMS["event_id_t"].values()]
    with open(os.path.join(ROOT, "yamcs", "etc", "events.json"), "w") as f:
        json.dump(ev, f, indent=2)
    os.makedirs(os.path.join(ROOT, "docs"), exist_ok=True)
    with open(os.path.join(ROOT, "docs", "mdb.md"), "w") as f:
        f.write(doc())
    print(f"s8.xml: {len(x.lines)} lines, {len(all_fields())} fields, "
          f"{len(EVENTS)} event layouts, {len(COMMANDS)} commands")


if __name__ == "__main__":
    main()
