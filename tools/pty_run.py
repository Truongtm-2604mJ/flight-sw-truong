#!/usr/bin/env python3
"""Run a command on a pseudo-terminal (SIS needs one) until a line matches
--until, or --timeout seconds; copy everything to --log.

    python3 pty_run.py --until "Timing finished" --timeout 600 --log x.log -- sis -leon3 -r a.exe
"""
import argparse
import os
import pty
import re
import select
import signal
import subprocess
import sys
import time
import tty

ap = argparse.ArgumentParser()
ap.add_argument("--until", required=True)
ap.add_argument("--timeout", type=float, default=600)
ap.add_argument("--log", required=True)
ap.add_argument("cmd", nargs=argparse.REMAINDER)
a = ap.parse_args()
cmd = a.cmd[1:] if a.cmd and a.cmd[0] == "--" else a.cmd
master, slave = pty.openpty()
tty.setraw(slave)
p = subprocess.Popen(cmd, stdin=slave, stdout=slave, stderr=slave, close_fds=True)
os.close(slave)
rx = re.compile(a.until)
buf, found, deadline = b"", False, time.monotonic() + a.timeout
with open(a.log, "wb") as log:
    while time.monotonic() < deadline and not found:
        r, _, _ = select.select([master], [], [], 0.5)
        if not r:
            if p.poll() is not None:
                break
            continue
        try:
            chunk = os.read(master, 4096)
        except OSError:
            break
        if not chunk:
            break
        log.write(chunk)
        buf += chunk
        found = rx.search(buf.decode(errors="replace")) is not None
if p.poll() is None:
    p.send_signal(signal.SIGTERM)
    try:
        p.wait(5)
    except subprocess.TimeoutExpired:
        p.kill()
print(("found" if found else "NOT found") + f": {a.until!r} -> {a.log}")
sys.exit(0 if found else 1)
