"""Ground side CCSDS Space Packet / PUS codec.

Written independently of the onboard C code on purpose: the CRC comes
from the Python standard library (binascii.crc_hqx is the CCITT
polynomial 0x1021; started at 0xFFFF it is CRC-16/CCITT-FALSE), and the
headers are packed with struct. If the two ends agree, it is because
both follow the packet definition, not because they share code.
"""

import binascii
import struct
from dataclasses import dataclass

PUS_VERSION = 2
APID_CMD = 0x010      # TC destination; source of TM(1,x), TM(17,x)
APID_HK = 0x020       # housekeeping reports
APID_EVENT = 0x021    # event reports
APID_IDLE = 0x7FF

ACK_ACCEPTANCE = 0x1
ACK_START = 0x2
ACK_PROGRESS = 0x4
ACK_COMPLETION = 0x8

TM_SEC_HDR_LEN = 13
TC_SEC_HDR_LEN = 5

FAILURE_CODES = {
    1: "SP_VERSION",
    2: "LENGTH",
    3: "CHECKSUM",
    4: "PACKET_HEADER",
    5: "APID",
    6: "PUS_VERSION",
    7: "SERVICE",
    8: "SUBTYPE",
    9: "APP_DATA",
    16: "EXECUTION",
}

VERIFICATION_NAMES = {
    1: "acceptance OK",
    2: "acceptance FAILED",
    3: "start OK",
    7: "completion OK",
    8: "completion FAILED",
}

EVENT_SEVERITY = {1: "informative", 2: "low severity anomaly",
                  3: "medium severity anomaly", 4: "high severity anomaly"}

EVENT_NAMES = {0x0101: "TEMP_BAT_HIGH", 0x0102: "TEMP_BAT_NOMINAL"}


def crc16(data: bytes) -> int:
    return binascii.crc_hqx(data, 0xFFFF)


def build_tc(seq_count: int, service: int, subtype: int, app_data: bytes = b"",
             *, apid: int = APID_CMD,
             ack: int = ACK_ACCEPTANCE | ACK_COMPLETION,
             source_id: int = 0x0001) -> bytes:
    """Return one complete telecommand packet."""
    if not 0 <= apid <= 0x7FF:
        raise ValueError("APID does not fit 11 bits")
    if not 0 <= seq_count <= 0x3FFF:
        raise ValueError("sequence count does not fit 14 bits")
    if not 0 <= ack <= 0xF:
        raise ValueError("ack flags do not fit 4 bits")

    sec_hdr = struct.pack(">BBBH", (PUS_VERSION << 4) | ack, service, subtype,
                          source_id)
    data_field_len = len(sec_hdr) + len(app_data) + 2          # + CRC
    word0 = (0 << 13) | (1 << 12) | (1 << 11) | apid           # v0, TC, sec hdr
    word1 = (0b11 << 14) | seq_count                           # unsegmented
    primary = struct.pack(">HHH", word0, word1, data_field_len - 1)
    body = primary + sec_hdr + app_data
    return body + struct.pack(">H", crc16(body))


@dataclass
class Telemetry:
    raw: bytes
    apid: int
    seq_count: int
    service: int
    subtype: int
    dest_id: int
    time_coarse: int
    time_fine: int
    data: bytes

    @property
    def time(self) -> float:
        return self.time_coarse + self.time_fine / 65536.0

    @property
    def kind(self):
        return (self.service, self.subtype)


class PacketError(Exception):
    pass


def parse_tm(raw: bytes) -> Telemetry:
    """Validate and decode one telemetry packet. Raises PacketError."""
    if len(raw) < 6:
        raise PacketError("shorter than a primary header")
    word0, word1, length = struct.unpack(">HHH", raw[:6])
    if word0 >> 13 != 0:
        raise PacketError("packet version is not 0")
    if len(raw) != 6 + length + 1:
        raise PacketError(
            f"length field says {6 + length + 1} octets, got {len(raw)}")
    if (word0 >> 12) & 1 != 0:
        raise PacketError("packet type is TC, expected TM")
    if (word0 >> 11) & 1 != 1:
        raise PacketError("no secondary header")
    if len(raw) < 6 + TM_SEC_HDR_LEN + 2:
        raise PacketError("too short for a PUS TM packet")
    if crc16(raw[:-2]) != struct.unpack(">H", raw[-2:])[0]:
        raise PacketError("CRC mismatch")

    ver_status, service, subtype, _counter, dest, coarse, fine = struct.unpack(
        ">BBBHHIH", raw[6:6 + TM_SEC_HDR_LEN])
    if ver_status >> 4 != PUS_VERSION:
        raise PacketError("PUS version is not 2")

    return Telemetry(raw=raw, apid=word0 & 0x7FF, seq_count=word1 & 0x3FFF,
                     service=service, subtype=subtype, dest_id=dest,
                     time_coarse=coarse, time_fine=fine,
                     data=raw[6 + TM_SEC_HDR_LEN:-2])


# ---- Source data decoders ---------------------------------------------------

def decode_verification(tm: Telemetry) -> dict:
    out = {"request_id": tm.data[:4]}
    if tm.subtype in (2, 8) and len(tm.data) >= 5:
        out["code"] = tm.data[4]
    return out


HK_SID_PLATFORM = 1
_HK_FORMAT = ">BHhhBHHHIH"   # SID first, 20 octets in total


def decode_hk(tm: Telemetry) -> dict:
    if len(tm.data) != struct.calcsize(_HK_FORMAT):
        raise PacketError(f"HK report with {len(tm.data)} data octets")
    (sid, vbus, t_bat, t_obc, mode, tc_rej, tc_acc, tm_drop, uptime,
     exec_us) = struct.unpack(_HK_FORMAT, tm.data)
    return {"sid": sid, "vbus_mv": vbus, "t_bat_c": t_bat / 100.0,
            "t_obc_c": t_obc / 100.0, "mode": mode, "tc_rejected": tc_rej,
            "tc_accepted": tc_acc, "tm_dropped": tm_drop, "uptime_s": uptime,
            "hk_exec_us": exec_us}


def decode_event(tm: Telemetry) -> dict:
    event_id, param_id, value, limit = struct.unpack(">HBhh", tm.data)
    return {"event_id": event_id, "param_id": param_id,
            "value_c": value / 100.0, "limit_c": limit / 100.0}


def describe(tm: Telemetry) -> str:
    """One line, human readable summary of a TM packet."""
    head = (f"APID 0x{tm.apid:03X} seq {tm.seq_count:5d} "
            f"t={tm.time:9.3f}s TM({tm.service},{tm.subtype})")
    try:
        if tm.service == 1:
            v = decode_verification(tm)
            text = (f"{VERIFICATION_NAMES.get(tm.subtype, '?')} "
                    f"for request {v['request_id'].hex().upper()}")
            if "code" in v:
                text += (f", failure code {v['code']} "
                         f"({FAILURE_CODES.get(v['code'], '?')})")
        elif tm.kind == (17, 2):
            text = "connection test report (alive)"
        elif tm.kind == (3, 25):
            h = decode_hk(tm)
            text = (f"HK SID {h['sid']}: Vbus {h['vbus_mv']} mV, "
                    f"Tbat {h['t_bat_c']:.2f} C, Tobc {h['t_obc_c']:.2f} C, "
                    f"mode 0x{h['mode']:02X}, TC acc/rej "
                    f"{h['tc_accepted']}/{h['tc_rejected']}, "
                    f"TM drop {h['tm_dropped']}, up {h['uptime_s']} s, "
                    f"exec {h['hk_exec_us']} us")
        elif tm.service == 5:
            e = decode_event(tm)
            text = (f"EVENT [{EVENT_SEVERITY.get(tm.subtype, '?')}] "
                    f"{EVENT_NAMES.get(e['event_id'], hex(e['event_id']))}: "
                    f"param {e['param_id']} = {e['value_c']:.2f} C, "
                    f"limit {e['limit_c']:.2f} C")
        else:
            text = f"data {tm.data.hex().upper()}"
    except (struct.error, PacketError) as exc:
        text = f"undecodable source data ({exc}): {tm.data.hex().upper()}"
    return f"{head}  {text}"
