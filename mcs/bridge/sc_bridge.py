"""LOTUS-1 spacecraft link and ground security unit for Yamcs.

    Yamcs  --TCP 10025-->  [ sc_bridge: security unit ]  --TC: lines-->  spacecraft
    Yamcs  <--TCP 10015--  [ sc_bridge               ]  <--TM: lines--  (host sim or SIS)

Yamcs builds plain PUS-C telecommands (MDB ../yamcs/mdb/lotus1.xml) and never
sees a key. For each TC the bridge, standing for the ground segment's
security unit (an HSM in a real ground station):

  - TC(210,1) LOAD_KEY(slot): generates the new key, wraps it under the KEK
    (AES-128-CCM) and fills in nonce and wrapped key. Prints the key check
    value, to be compared with HK SID 3 before activation.
  - TC(210,10) / TC(210,11): signs with the RECOVERY key (SPI 0) and the
    challenge from the latest HK SID 3. A floor of 0 in TC(210,10) is
    replaced with the safe floor (above every counter value ever used).
  - everything else: signs with the active key and the next counter.
It keeps the Yamcs sequence count, so Yamcs matches TM(1,x) to its command.
It follows HK SID 3: when the spacecraft reports another active slot that
the ground holds a key for, it switches to it.

    python3 sc_bridge.py --spawn ../../build/host/lotus1-test.host
    python3 sc_bridge.py --pty --spawn "sparc-rtems6-sis -leon3 -rt -r ../../build/rtems/lotus1-test.exe"

Port 10026 (--raw-port) stands for the RF uplink itself: whatever is sent
there reaches the spacecraft WITHOUT the security unit, as a transmitter
on the uplink frequency would. demo/run_demo.py uses it to show a forged
command being rejected; nothing in operations uses it.
"""
import argparse
import os
import socket
import struct
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "ground"))

import seclink    # noqa: E402
import secdecode  # noqa: E402
import secunit    # noqa: E402
import pus        # noqa: E402


class TmServer:
    """Accepts Yamcs' TcpTmDataLink and sends it every TM packet."""

    def __init__(self, port, log):
        self.clients = []
        self.lock = threading.Lock()
        self.log = log
        self.sock = socket.create_server(("127.0.0.1", port), reuse_port=False)
        threading.Thread(target=self._accept, daemon=True).start()

    def _accept(self):
        while True:
            c, addr = self.sock.accept()
            with self.lock:
                self.clients.append(c)
            self.log(f"# TM client connected from {addr}")

    def send(self, packet):
        with self.lock:
            for c in list(self.clients):
                try:
                    c.sendall(packet)
                except OSError:
                    self.clients.remove(c)
                    self.log("# TM client disconnected")


class Bridge:
    def __init__(self, args):
        self.args = args
        self.link = seclink.SecureLink(args, state_file=args.ground_state)
        self.su = self.link.su
        self.sid3 = None
        self.lock = threading.Lock()
        self.tm = TmServer(args.tm_port, self.link.log)
        self.tc_sock = socket.create_server(("127.0.0.1", args.tc_port))
        threading.Thread(target=self._tc_accept, daemon=True).start()
        self.raw_sock = socket.create_server(("127.0.0.1", args.raw_port))
        threading.Thread(target=self._raw_accept, daemon=True).start()
        self.link.log(f"# bridge: TM on {args.tm_port}, TC on {args.tc_port}, active "
                      f"slot {self.su.active}, next counter {self.su.next_seq(self.su.active)}")

    # -- telecommands from Yamcs ----------------------------------------------

    def _tc_accept(self):
        while True:
            c, addr = self.tc_sock.accept()
            self.link.log(f"# TC client connected from {addr}")
            threading.Thread(target=self._tc_reader, args=(c,), daemon=True).start()

    def _raw_accept(self):
        while True:
            c, addr = self.raw_sock.accept()
            try:
                while True:
                    hdr = self._read(c, 6)
                    pkt = hdr + self._read(c, struct.unpack(">H", hdr[4:6])[0] + 1)
                    self.link.send_raw(pkt, "!! RAW UPLINK (not through the security unit)")
            except (ConnectionError, OSError):
                c.close()

    @staticmethod
    def _read(c, n):
        buf = b""
        while len(buf) < n:
            chunk = c.recv(n - len(buf))
            if not chunk:
                raise ConnectionError
            buf += chunk
        return buf

    def _tc_reader(self, c):
        try:
            while True:
                hdr = self._read(c, 6)
                length = struct.unpack(">H", hdr[4:6])[0] + 1
                pkt = hdr + self._read(c, length)
                try:
                    self.forward(pkt)
                except Exception as exc:          # noqa: BLE001 (keep the link up)
                    self.link.log(f"# TC NOT SENT: {exc}")
        except (ConnectionError, OSError):
            self.link.log("# TC client disconnected")

    @staticmethod
    def rebuild(pkt, data):
        """Same headers, new application data (CRC recomputed)."""
        body = bytearray(pkt[:11] + data)
        struct.pack_into(">H", body, 4, len(body) + 2 - 7)
        return bytes(body) + struct.pack(">H", pus.crc16(bytes(body)))

    def forward(self, pkt):
        if pus.crc16(pkt[:-2]) != struct.unpack(">H", pkt[-2:])[0]:
            raise ValueError("CRC from Yamcs does not check")
        service, subtype = pkt[7], pkt[8]
        data = pkt[11:-2]
        spi = seq = None
        note = f"TC({service},{subtype}) from Yamcs"
        with self.lock:
            if (service, subtype) == (secunit.SVC_SEC, secunit.TC_LOAD_KEY):
                slot = data[0]
                _s, _st, newdata, key = self.su.tc_load_key(slot)
                pkt = self.rebuild(pkt, newdata)
                note += (f" LOAD_KEY slot {slot}: new key wrapped, ground KCV "
                         f"{secunit.kcv(key):08X} (compare with SID 3 before ACTIVATE)")
            elif service == secunit.SVC_SEC and subtype in (secunit.TC_RECOVERY_COUNTER,
                                                             secunit.TC_RECOVERY_ACTIVATE):
                if self.sid3 is None:
                    raise ValueError("no HK SID 3 received yet: challenge unknown")
                spi, seq = secunit.SPI_RECOVERY, self.sid3["challenge"]
                if subtype == secunit.TC_RECOVERY_COUNTER:
                    slot, floor = struct.unpack(">BI", data[:5])
                    if floor == 0:
                        floor = self.su.safe_floor(slot)
                        pkt = self.rebuild(pkt, struct.pack(">BI", slot, floor))
                    self.su.set_next_seq(slot, floor + 1)
                    note += f" RECOVERY counter slot {slot} -> {floor}"
                note += f" [recovery key, challenge {seq}]"
            secured = self.su.protect(pkt, spi=spi, seq=seq)
        self.link.send_raw(secured, note)

    # -- telemetry -------------------------------------------------------------------

    def run(self):
        last_status = 0.0
        while True:
            tm = self.link.poll(0.5)
            if tm is not None:
                self.tm.send(tm.raw)
                if tm.apid == secdecode.APID_SEC and tm.kind == (3, 25):
                    self.on_sid3(secdecode.decode_sid3(tm.data))
                self.link.telemetry.clear()      # nothing is kept: run for days
            if self.link.exited():
                self.link.log("# spacecraft process ended (reset): restarting, NV kept")
                time.sleep(1.0)
                self.link.restart(keep_nv=True)
            if time.monotonic() - last_status > 30 and self.sid3:
                last_status = time.monotonic()
                self.link.log(f"# status: onboard active slot {self.sid3['active']}, "
                              f"counter {self.sid3['counter'][self.sid3['active']]}, ground "
                              f"next {self.su.next_seq(self.su.active)}")

    def on_sid3(self, h):
        with self.lock:
            if h["untrusted"].get(h["active"]) and (self.sid3 is None or
                                                   not self.sid3["untrusted"].get(h["active"])):
                self.link.log(f"# FIRST CONTACT NEEDED: slot {h['active']} counter untrusted "
                              f"(factory key store). From Yamcs: RECOVERY_SET_COUNTER slot "
                              f"SLOT_{h['active']} floor 0, then accept it in the supervised "
                              f"queue. Safe floor here: {self.su.safe_floor(h['active'])}")
            self.sid3 = h
            a = h["active"]
            if a != self.su.active and self.su.key(a) is not None and \
                    h["state"].get(a) == 2:
                self.link.log(f"# SID 3: slot {a} is now ACTIVE onboard; the security "
                              f"unit follows")
                self.su.activated(a)
            # Never send a counter the spacecraft would take as a replay.
            onboard = h["counter"].get(self.su.active, 0)
            if not h["untrusted"].get(self.su.active) and \
                    self.su.next_seq(self.su.active) <= onboard < 0xFFFF0000:
                self.su.set_next_seq(self.su.active, onboard + 1)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    seclink.SecureLink.add_arguments(p)
    p.add_argument("--tm-port", type=int, default=10015)
    p.add_argument("--tc-port", type=int, default=10025)
    p.add_argument("--raw-port", type=int, default=10026,
                   help="unprotected uplink, for the forged-command demonstration")
    p.add_argument("--ground-state", default=None,
                   help="security unit state file (default: next to --nv-file)")
    p.set_defaults(nv_file=os.path.join(HERE, "..", ".run", "spacecraft_nv.bin"))
    args = p.parse_args()
    os.makedirs(os.path.dirname(os.path.abspath(args.nv_file)), exist_ok=True)
    Bridge(args).run()


if __name__ == "__main__":
    main()
