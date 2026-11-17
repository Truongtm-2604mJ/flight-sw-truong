"""Ground link for Session 5. Same framing and transports as Session 4
(TC:<hex> / TM:<hex> lines over a child process, a pty, or TCP), plus:

  - the spacecraft can be restarted from the ground script (a fresh run
    per campaign row, and the relaunch after a watchdog reset);
  - one log file for the whole session, appended across restarts;
  - telemetry is described with the Session 5 decoders.
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

import fdir
import pus


class Link:
    def __init__(self, args):
        self.args = args
        self._t0 = time.monotonic()
        self._log = open(args.log, "w") if args.log else None
        self._tc_seq = {}
        self.telemetry = []
        self.errors = []
        self._proc = None
        self._pty = None
        self._sock = None
        self.start()

    @staticmethod
    def add_arguments(parser):
        parser.add_argument("--spawn", help="command that runs the spacecraft")
        parser.add_argument("--pty", action="store_true",
                            help="run the child on a pseudo-terminal (SIS)")
        parser.add_argument("--tcp", help="host:port of the console UART")
        parser.add_argument("--log", help="exchange log file")
        parser.add_argument("--nv-file", default="fdir_nv.bin",
                            help="host simulation: file standing for the "
                                 "non volatile memory")

    # -- process control -----------------------------------------------------

    def start(self):
        a = self.args
        self._rx = queue.Queue()
        env = dict(os.environ, FDIR_NV_FILE=os.path.abspath(a.nv_file))
        if a.spawn and a.pty:
            master, slave = pty.openpty()
            tty.setraw(slave)
            self._proc = subprocess.Popen(shlex.split(a.spawn), stdin=slave,
                                          stdout=slave, stderr=slave,
                                          close_fds=True, env=env)
            os.close(slave)
            self._pty = master
            self._write = lambda data: os.write(master, data)
            source = os.fdopen(master, "rb", buffering=0, closefd=False)
            what = f"child process on a pty: {a.spawn}"
        elif a.spawn:
            self._proc = subprocess.Popen(shlex.split(a.spawn),
                                          stdin=subprocess.PIPE,
                                          stdout=subprocess.PIPE, bufsize=0,
                                          env=env)
            self._write = self._proc.stdin.write
            source = self._proc.stdout
            what = f"child process: {a.spawn}"
        elif a.tcp:
            host, port = a.tcp.rsplit(":", 1)
            self._sock = socket.create_connection((host, int(port)))
            self._write = self._sock.sendall
            source = self._sock.makefile("rb", buffering=0)
            what = f"tcp {a.tcp}"
        else:
            raise SystemExit("choose a transport: --spawn or --tcp")
        self.log(f"# spacecraft started ({what})")
        threading.Thread(target=self._reader, args=(source, self._rx),
                         daemon=True).start()

    def stop(self):
        if self._proc is not None and self._proc.poll() is None:
            self._proc.kill()
            self._proc.wait()
        if self._pty is not None:
            os.close(self._pty)
            self._pty = None
        if self._sock is not None:
            self._sock.close()
            self._sock = None

    def restart(self, keep_nv):
        """Stop the spacecraft and start it again. keep_nv=False starts
        from blank non volatile memory (a cold first boot)."""
        self.stop()
        if not keep_nv and os.path.exists(self.args.nv_file):
            os.remove(self.args.nv_file)
        self.start()

    def exited(self):
        return self._proc is not None and self._proc.poll() is not None

    def close(self):
        self.stop()
        if self._log:
            self._log.close()

    def _reader(self, source, rx):
        buf = b""
        while True:
            try:
                chunk = source.read(256)
            except (OSError, ValueError):
                chunk = b""
            if not chunk:
                rx.put(None)
                return
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                rx.put(line.strip(b"\r\x00 "))

    # -- logging ---------------------------------------------------------------

    def log(self, text):
        line = f"[{time.monotonic() - self._t0:8.3f}] {text}"
        print(line, flush=True)
        if self._log:
            self._log.write(line + "\n")
            self._log.flush()

    # -- telecommands ------------------------------------------------------------

    def next_tc_seq(self, apid=fdir.APID_CMD):
        seq = self._tc_seq.get(apid, 0)
        self._tc_seq[apid] = (seq + 1) & 0x3FFF
        return seq

    def send(self, cmd, note="", **kwargs):
        """cmd is a (service, subtype, app_data) tuple from fdir.py."""
        service, subtype, data = cmd
        packet = pus.build_tc(self.next_tc_seq(), service, subtype, data,
                              **kwargs)
        self.send_raw(packet, f"TC({service},{subtype}) {note}".strip())
        return packet

    def send_raw(self, packet, note=""):
        self.log(f"TC -> {packet.hex().upper()}  {note}")
        self._write(b"TC:" + packet.hex().upper().encode() + b"\n")

    # -- telemetry ---------------------------------------------------------------

    def poll(self, timeout):
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
                self._rx.put(None)       # sticky until the next start()
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
            tm.wall = time.monotonic()
            self.telemetry.append(tm)
            self.log(f"TM <- {tm.raw.hex().upper()}")
            self.log(f"        {fdir.describe(tm)}")
            return tm

    def wait_for(self, predicate, timeout):
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            tm = self.poll(remaining)
            if tm is not None and predicate(tm):
                return tm

    def collect(self, duration):
        got = []
        deadline = time.monotonic() + duration
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return got
            tm = self.poll(remaining)
            if tm is not None:
                got.append(tm)

    def wait_exit(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.poll(0.2)
            if self.exited():
                return True
        return False


class Checker:
    def __init__(self, link):
        self.link = link
        self.passed = 0
        self.failed = 0

    def check(self, cond, text):
        if cond:
            self.passed += 1
            self.link.log(f"  PASS  {text}")
        else:
            self.failed += 1
            self.link.log(f"  FAIL  {text}")
        return bool(cond)

    def check_sequences(self, since=0):
        """Sequence counts consecutive per APID, within one spacecraft run
        (counters restart from 0 at every boot)."""
        runs = []
        current = []
        for tm in self.link.telemetry[since:]:
            current.append(tm)
        by_apid = {}
        for tm in current:
            by_apid.setdefault(tm.apid, []).append(tm.seq_count)
        for apid, seqs in sorted(by_apid.items()):
            ok = all((b - a) & 0x3FFF == 1 for a, b in zip(seqs, seqs[1:]))
            runs.append(self.check(ok, f"APID 0x{apid:03X}: {len(seqs)} "
                                       f"packets, sequence counts consecutive"))
        return all(runs)

    def finish(self):
        self.link.log(f"# result: {self.passed} passed, {self.failed} failed")
        self.link.close()
        sys.exit(1 if self.failed else 0)
