"""Session 8 ground security unit.

Sits between the mission control system (or a test script) and the
spacecraft. Every telecommand gets the security trailer

    | packet without CRC | SPI 1 | SEQ 4 | MAC 16 | CRC 2 |

with the packet length field corrected. The MAC is HMAC-SHA-256 truncated
to 128 bits, from the Python standard library (hmac, hashlib); keys are
wrapped for upload with AES-128-CCM from the 'cryptography' package. None
of this shares code with the onboard C (tinycrypt): if the two ends agree,
it is because both follow the format in lab-8.1-authentication/tc_auth.h.

State (which slot is active, the next counter per slot, the keys loaded
into each slot) is kept in a JSON file, so counters stay monotonic across
ground sessions. That file holds key material: on a real ground segment
it lives in the hardware security module, not on disk (security-report.md).
"""

import binascii
import hashlib
import hmac
import json
import os
import struct

KEYS_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                         "keys", "lab-keys.json")
TRAILER_LEN = 1 + 4 + 16
SPI_RECOVERY = 0
WINDOW = 65536

SVC_SEC = 210
TC_LOAD_KEY, TC_ACTIVATE, TC_ERASE = 1, 2, 3
TC_RECOVERY_COUNTER, TC_RECOVERY_ACTIVATE = 10, 11


def crc16(data):
    return binascii.crc_hqx(data, 0xFFFF)


def mac16(key, data):
    return hmac.new(key, data, hashlib.sha256).digest()[:16]


def kcv(key):
    """Key check value: first 4 octets of HMAC-SHA-256(key, "KCV")."""
    return struct.unpack(">I", hmac.new(key, b"KCV", hashlib.sha256).digest()[:4])[0]


def add_trailer(packet, spi, seq, key):
    """packet: a complete TC (with CRC). Returns the secured TC."""
    body = bytearray(packet[:-2])
    total = len(body) + TRAILER_LEN + 2
    struct.pack_into(">H", body, 4, total - 7)          # packet length field
    body += struct.pack(">BI", spi, seq & 0xFFFFFFFF)
    body += mac16(key, bytes(body))
    return bytes(body) + struct.pack(">H", crc16(bytes(body)))


def strip_trailer(packet):
    """Inverse of add_trailer, for scheduled (inner) commands and tests."""
    body = bytearray(packet[:-2 - TRAILER_LEN])
    struct.pack_into(">H", body, 4, len(body) + 2 - 7)
    return bytes(body) + struct.pack(">H", crc16(bytes(body)))


class SecurityUnit:
    def __init__(self, state_file=None, keys_file=KEYS_FILE):
        with open(keys_file) as f:
            self.lab = json.load(f)
        self.recovery_key = bytes.fromhex(self.lab["recovery_key"])
        self.kek = bytes.fromhex(self.lab["kek"])
        self.state_file = state_file
        self.state = None
        if state_file and os.path.exists(state_file):
            with open(state_file) as f:
                self.state = json.load(f)
        if self.state is None:
            self.reset_to_factory()

    # -- state -------------------------------------------------------------

    def reset_to_factory(self):
        """Ground view of a spacecraft whose key store is in factory state."""
        self.state = {"active": 1,
                      "keys": {"1": self.lab["provisioned_key"], "2": None},
                      "next_seq": {"1": 1, "2": 1},
                      "highest_used": {"1": 0, "2": 0},
                      "spare_index": 0, "nonce": 1}
        self.save()

    def save(self):
        if self.state_file:
            tmp = self.state_file + ".tmp"
            with open(tmp, "w") as f:
                json.dump(self.state, f, indent=1)
            os.replace(tmp, self.state_file)

    @property
    def active(self):
        return self.state["active"]

    def key(self, slot):
        if slot == SPI_RECOVERY:
            return self.recovery_key
        k = self.state["keys"].get(str(slot))
        return bytes.fromhex(k) if k else None

    def next_seq(self, slot):
        return self.state["next_seq"][str(slot)]

    def set_next_seq(self, slot, value):
        self.state["next_seq"][str(slot)] = value
        self.save()

    # -- protecting commands -----------------------------------------------

    def protect(self, packet, spi=None, seq=None, key=None):
        """Secure one TC. Defaults: the active slot, its next counter, its key.
        Any of them can be forced (negative tests)."""
        spi = self.active if spi is None else spi
        if seq is None:
            if spi == SPI_RECOVERY:
                raise ValueError("recovery commands need the challenge from HK SID 3")
            seq = self.next_seq(spi)
            s = str(spi)
            self.state["next_seq"][s] = seq + 1
            self.state["highest_used"][s] = max(self.state["highest_used"][s], seq)
            self.save()
        elif spi != SPI_RECOVERY:
            # Forced values went out on the uplink too: the high-water mark
            # covers every value ever transmitted, whatever key signed it.
            s = str(spi)
            if s in self.state["highest_used"]:
                self.state["highest_used"][s] = max(self.state["highest_used"][s],
                                                    seq & 0xFFFFFFFF)
                self.save()
        if key is None:
            key = self.key(spi)
            if key is None:
                raise ValueError(f"no key known for slot {spi}")
        return add_trailer(packet, spi, seq, key)

    # -- key management commands (service 210) ------------------------------

    def new_key(self):
        spares = self.lab["spare_keys"]
        i = self.state["spare_index"]
        self.state["spare_index"] = i + 1
        self.save()
        if i < len(spares):
            return bytes.fromhex(spares[i])
        return os.urandom(32)

    def wrap_key(self, slot, key, nonce=None):
        from cryptography.hazmat.primitives.ciphers.aead import AESCCM
        if nonce is None:
            n = self.state["nonce"]
            self.state["nonce"] = n + 1
            self.save()
            nonce = b"\x00" * 5 + struct.pack(">Q", n)      # never repeats
        wrapped = AESCCM(self.kek, tag_length=16).encrypt(nonce, key,
                                                          b"KEY" + bytes([slot]))
        return nonce, wrapped

    def tc_load_key(self, slot, key=None, corrupt=False):
        """(service, subtype, data) for TC(210,1); records the key as loaded
        in that slot (the ground copy) unless corrupt."""
        key = self.new_key() if key is None else key
        nonce, wrapped = self.wrap_key(slot, key)
        if corrupt:
            wrapped = bytes([wrapped[0] ^ 1]) + wrapped[1:]
        else:
            self.state["keys"][str(slot)] = key.hex()
            self.state["next_seq"][str(slot)] = 1
            self.state["highest_used"][str(slot)] = 0
            self.save()
        return SVC_SEC, TC_LOAD_KEY, bytes([slot]) + nonce + wrapped, key

    @staticmethod
    def tc_activate(slot):
        return SVC_SEC, TC_ACTIVATE, bytes([slot])

    @staticmethod
    def tc_erase(slot):
        return SVC_SEC, TC_ERASE, bytes([slot])

    @staticmethod
    def tc_recovery_counter(slot, floor):
        return SVC_SEC, TC_RECOVERY_COUNTER, struct.pack(">BI", slot, floor)

    @staticmethod
    def tc_recovery_activate(slot):
        return SVC_SEC, TC_RECOVERY_ACTIVATE, bytes([slot])

    def activated(self, slot):
        """Call when HK SID 3 confirms the switch."""
        self.state["active"] = slot
        self.save()

    def safe_floor(self, slot):
        """Counter floor for a recovery reset: above every value the ground
        ever used for that slot, so no recorded command becomes valid."""
        return self.state["highest_used"][str(slot)] + 1
