"""Ground station link to the spacecraft, plus a session logger.

Framing is one packet per text line, hex encoded:
    ground -> spacecraft   TC:<hex>
    spacecraft -> ground   TM:<hex>
Lines without the TM: prefix are console text (boot banner, simulator
messages) and are logged but otherwise ignored.

Transports:
    --spawn "<command>"   start the spacecraft as a child process and talk
                          over its stdin/stdout through pipes (host
                          simulation)
    --spawn ... --pty     same, but give the child a pseudo-terminal.
                          Needed for SIS: it only forwards its stdin to
                          the simulated UART when stdin is a terminal.
    --tcp host:port       connect to a TCP socket carrying the console
                          UART (for example QEMU "-serial tcp:...")
"""

import argparse
import os
import pty
import queue
import shlex
import socket
import subprocess
import sys
import threading
import time
import tty

import pus


class Link:
    def __init__(self, log_path=None):
        self._rx = queue.Queue()
        self._t0 = time.monotonic()
        self._log = open(log_path, "w") if log_path else None
        self._tc_seq = {}          # per APID telecommand sequence counters
        self.telemetry = []        # every valid TM received, in order
        self.errors = []           # undecodable TM lines

    # -- transports ----------------------------------------------------------

    @classmethod
    def from_args(cls, args):
        link = cls(args.log)
        if args.spawn and args.pty:
            link.log(f"# transport: child process on a pty: {args.spawn}")
            master, slave = pty.openpty()
            tty.setraw(slave)      # no echo, no CR/LF translation
            link._proc = subprocess.Popen(
                shlex.split(args.spawn), stdin=slave, stdout=slave,
                stderr=slave, close_fds=True)
            os.close(slave)
            link._pty = master
            link._write = lambda data: os.write(master, data)
            source = os.fdopen(master, "rb", buffering=0, closefd=False)
        elif args.spawn:
            link.log(f"# transport: child process: {args.spawn}")
            link._proc = subprocess.Popen(
                shlex.split(args.spawn), stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, bufsize=0)
            link._write = link._proc.stdin.write
            source = link._proc.stdout
        elif args.tcp:
            host, port = args.tcp.rsplit(":", 1)
            link.log(f"# transport: tcp {host}:{port}")
            link._sock = socket.create_connection((host, int(port)))
            link._write = link._sock.sendall
            source = link._sock.makefile("rb", buffering=0)
        else:
            raise SystemExit("choose a transport: --spawn or --tcp")
        threading.Thread(target=link._reader, args=(source,),
                         daemon=True).start()
        return link

    @staticmethod
    def add_arguments(parser: argparse.ArgumentParser):
        parser.add_argument("--spawn", help="command that runs the spacecraft")
        parser.add_argument("--pty", action="store_true",
                            help="with --spawn: run the child on a "
                                 "pseudo-terminal (required for SIS)")
        parser.add_argument("--tcp", help="host:port of the console UART")
        parser.add_argument("--log", help="write the exchange log to this file")

    def _reader(self, source):
        buf = b""
        while True:
            try:
                chunk = source.read(256)
            except OSError:        # pty closed: the child has exited
                chunk = b""
            if not chunk:
                self._rx.put(None)
                return
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self._rx.put(line.strip(b"\r\x00 "))

    def close(self):
        if getattr(self, "_pty", None) is not None:
            self._proc.kill()      # a simulator on a pty has no EOF to see
            self._proc.wait()
            os.close(self._pty)
        elif getattr(self, "_proc", None):
            try:
                self._proc.stdin.close()
                self._proc.wait(timeout=2)
            except Exception:
                self._proc.kill()
        if getattr(self, "_sock", None):
            self._sock.close()
        if self._log:
            self._log.close()

    # -- logging -------------------------------------------------------------

    def log(self, text):
        line = f"[{time.monotonic() - self._t0:8.3f}] {text}"
        print(line)
        if self._log:
            self._log.write(line + "\n")
            self._log.flush()

    # -- telecommands ----------------------------------------------------------

    def next_tc_seq(self, apid=pus.APID_CMD):
        seq = self._tc_seq.get(apid, 0)
        self._tc_seq[apid] = (seq + 1) & 0x3FFF
        return seq

    def send_tc(self, service, subtype, app_data=b"", *, note="", **kwargs):
        """Build, log and send a telecommand. Returns the packet."""
        apid = kwargs.get("apid", pus.APID_CMD)
        packet = pus.build_tc(self.next_tc_seq(apid), service, subtype,
                              app_data, **kwargs)
        self.send_raw(packet, f"TC({service},{subtype}) {note}".strip())
        return packet

    def send_raw(self, packet: bytes, note=""):
        self.log(f"TC -> {packet.hex().upper()}  {note}")
        self._write(b"TC:" + packet.hex().upper().encode() + b"\n")

    # -- telemetry -------------------------------------------------------------

    def poll(self, timeout):
        """Wait up to `timeout` s for one TM packet. Returns it or None."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            try:
                line = self._rx.get(timeout=remaining)
            except queue.Empty:
                return None
            if line is None:
                self.log("# link closed by the spacecraft side")
                self._rx.put(None)
                time.sleep(min(remaining, 0.05))
                return None
            if not line.startswith(b"TM:"):
                if line:
                    self.log(f"console  {line.decode(errors='replace')}")
                continue
            try:
                tm = pus.parse_tm(bytes.fromhex(line[3:].decode()))
            except (ValueError, pus.PacketError) as exc:
                self.errors.append(line)
                self.log(f"TM <- {line[3:].decode(errors='replace')}  "
                         f"REJECTED BY GROUND: {exc}")
                continue
            self.telemetry.append(tm)
            self.log(f"TM <- {tm.raw.hex().upper()}")
            self.log(f"        {pus.describe(tm)}")
            return tm

    def wait_for(self, predicate, timeout):
        """Receive until a TM packet satisfies `predicate`. Returns that
        packet, or None after `timeout` s (wall clock)."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            tm = self.poll(remaining)
            if tm is not None and predicate(tm):
                return tm

    def collect(self, duration, apid=None):
        """Receive for `duration` s. Returns the TM packets (optionally
        only those from one APID) that arrived in that window."""
        got = []
        deadline = time.monotonic() + duration
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return got
            tm = self.poll(remaining)
            if tm is not None and (apid is None or tm.apid == apid):
                got.append(tm)


class Checker:
    """Collects pass/fail results so a script can exit non zero."""

    def __init__(self, link):
        self.link = link
        self.failed = 0
        self.passed = 0

    def check(self, condition, text):
        if condition:
            self.passed += 1
            self.link.log(f"  PASS  {text}")
        else:
            self.failed += 1
            self.link.log(f"  FAIL  {text}")
        return condition

    def check_sequence(self, apid):
        """Sequence counts of all TM from `apid` are consecutive."""
        seqs = [tm.seq_count for tm in self.link.telemetry if tm.apid == apid]
        ok = all((b - a) & 0x3FFF == 1 for a, b in zip(seqs, seqs[1:]))
        shown = ", ".join(str(s) for s in seqs[:12])
        if len(seqs) > 12:
            shown += f", ... {seqs[-1]}"
        return self.check(ok and len(seqs) > 0,
                          f"APID 0x{apid:03X}: {len(seqs)} packets, sequence "
                          f"counts consecutive ({shown})")

    def finish(self):
        self.link.log(f"# result: {self.passed} passed, {self.failed} failed")
        self.link.close()
        sys.exit(1 if self.failed else 0)
