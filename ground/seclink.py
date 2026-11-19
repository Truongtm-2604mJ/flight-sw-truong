"""LOTUS-1 ground link: the Session 5 link (TC:/TM: lines over a child
process, a pty or TCP) with every telecommand passed through the ground
security unit, and security telemetry (APID 0x022) decoded.

    link = SecureLink(args)          # args from SecureLink.add_arguments
    link.send((8, 1, b"\\x01\\x02"))  # authenticated with the active key
    link.send(cmd, spi=2, seq=7, key=b"...")   # forced, for negative tests
    link.send_plain(cmd)             # no trailer at all

install() patches the Session 5 Link class itself, so the unchanged
Session 5 ground scripts (gs_modes.py, campaign.py, check_flight.py) can run
against the Session 8 build: run_secure.py does that.
"""

import os
import sys
import time

import fdir      # noqa: E402  (Session 5 codec, which imports Session 4 pus.py)
import link      # noqa: E402  (Session 5 link)
import pus       # noqa: E402

import secdecode  # noqa: E402
import secunit    # noqa: E402

pus.FAILURE_CODES.update({10: "AUTH", 11: "REPLAY", 12: "WINDOW", 13: "SEC_STATE"})

_describe_s5 = fdir.describe


def describe(tm):
    if tm.apid == secdecode.APID_SEC:
        return secdecode.describe(tm)
    return _describe_s5(tm)


fdir.describe = describe


def _secure_send(self, cmd, note="", spi=None, seq=None, key=None, **kwargs):
    service, subtype, data = cmd[:3]
    plain = pus.build_tc(self.next_tc_seq(), service, subtype, data, **kwargs)
    packet = self.su.protect(plain, spi=spi, seq=seq, key=key)
    tag = f"[SPI {self.su.active if spi is None else spi}]"
    self.send_raw(packet, f"TC({service},{subtype}) {tag} {note}".strip())
    return packet


def _send_plain(self, cmd, note="", **kwargs):
    service, subtype, data = cmd[:3]
    packet = pus.build_tc(self.next_tc_seq(), service, subtype, data, **kwargs)
    self.send_raw(packet, f"TC({service},{subtype}) [no trailer] {note}".strip())
    return packet


def default_state_file(args):
    nv = getattr(args, "nv_file", None) or "fdir_nv.bin"
    return os.path.abspath(nv) + ".ground.json"


_ORIG_POLL = link.Link.poll


def first_contact(lnk, timeout=6.0):
    """Returns True if a first contact was needed and done, False otherwise
    (not needed, or failed: then normal commands are refused, and the log
    says why).

    If HK SID 3 says the active slot's counter is untrusted (factory key
    store), set its floor with the recovery key: the ground's high-water
    mark + 1, so nothing recorded earlier becomes valid. Returns True if the
    link is ready for normal commanding."""
    deadline = time.monotonic() + timeout
    h = None
    while time.monotonic() < deadline and h is None:
        tm = _ORIG_POLL(lnk, deadline - time.monotonic())
        if tm is not None and tm.apid == secdecode.APID_SEC and tm.kind == (3, 25):
            h = secdecode.decode_sid3(tm.data)
    if h is None:
        lnk.log("# first contact: no HK SID 3 received")
        return False
    slot = h["active"]
    if not h["untrusted"].get(slot):
        return False
    floor = max(lnk.su.safe_floor(slot), h["counter"][slot])
    lnk.log(f"# FIRST CONTACT: slot {slot} counter untrusted (factory key store): "
            f"RECOVERY_SET_COUNTER floor {floor}, challenge {h['challenge']}")
    tc = lnk.send(secunit.SecurityUnit.tc_recovery_counter(slot, floor),
                  "first contact", spi=secunit.SPI_RECOVERY, seq=h["challenge"])
    lnk.su.set_next_seq(slot, floor + 1)
    while time.monotonic() < deadline + 3.0:
        tm = _ORIG_POLL(lnk, 0.5)
        if tm is not None and tm.service == 1 and tm.data[:4] == tc[:4] and \
                tm.subtype in (2, 7, 8):
            return tm.subtype == 7
    return False


class SecureLink(link.Link):
    def __init__(self, args, state_file=None):
        self.su = secunit.SecurityUnit(state_file or default_state_file(args))
        super().__init__(args)

    send = _secure_send
    send_plain = _send_plain

    def first_contact(self, timeout=6.0):
        """True if the first contact was needed and done."""
        return first_contact(self, timeout)

    def latest_sid3(self, timeout=3.0):
        tm = self.wait_for(lambda t: t.apid == secdecode.APID_SEC and
                           t.kind == (3, 25), timeout)
        return secdecode.decode_sid3(tm.data) if tm else None

    def sec_events(self, since=0, name=None):
        out = []
        for tm in self.telemetry[since:]:
            if tm.apid == secdecode.APID_SEC and tm.service == 5:
                e = secdecode.decode_event(tm.data)
                if name is None or e["name"] == name:
                    out.append(e)
        return out


def install(state_file=None):
    """Make every Session 5 Link a secure one."""
    orig_init = link.Link.__init__

    def init(self, args):
        self.su = secunit.SecurityUnit(state_file or default_state_file(args))
        orig_init(self, args)
        # A factory key store accepts nothing before the first contact. Do it,
        # then restart cold (FDIR NV removed, key store NV kept), so the script
        # sees the boot it expects, with a commissioned key store.
        if first_contact(self) and getattr(self, "_proc", None) is not None:
            self.log("# first contact done: cold restart for the script")
            self.restart(keep_nv=False)
            self.telemetry = []

    link.Link.__init__ = init
    link.Link.send = _secure_send
    link.Link.send_plain = _send_plain

    # The Session 5 scripts know APIDs 0x010, 0x020 and 0x021 only, and some
    # select HK by (3,25) alone: security telemetry is logged, not returned.
    orig_poll = link.Link.poll

    def poll(self, timeout):
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            tm = orig_poll(self, remaining)
            if tm is None or tm.apid != secdecode.APID_SEC:
                return tm
            self.telemetry.remove(tm)

    link.Link.poll = poll
