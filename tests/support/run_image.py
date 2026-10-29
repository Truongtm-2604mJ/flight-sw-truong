#!/usr/bin/env python3
"""Run a self checking test image (host simulation or SIS) and turn its
termination marker into an exit code, so CI never needs a human to read
the console.

    python3 run_image.py --timeout 300 --log out.log -- sparc-rtems6-sis -leon3 -r int.exe

The image must print exactly one line
    @@ INTEGRATION RESULT: PASS|FAIL <passed>/<run> @@
Exit codes: 0 PASS, 1 FAIL, 2 timeout (no marker), 3 the image ended
without a marker. The simulator is stopped as soon as the marker is seen,
whether or not it exits by itself.
"""
import argparse
import os
import re
import select
import signal
import subprocess
import sys
import time

MARKER = re.compile(r"@@ INTEGRATION RESULT: (PASS|FAIL) (\d+)/(\d+)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--timeout", type=float, default=300)
    ap.add_argument("--log", required=True)
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == "--" else a.cmd
    os.makedirs(os.path.dirname(os.path.abspath(a.log)), exist_ok=True)
    start = time.monotonic()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         stdin=subprocess.DEVNULL, start_new_session=True)
    result, buf = None, b""
    with open(a.log, "wb") as log:
        while True:
            left = a.timeout - (time.monotonic() - start)
            if left <= 0:
                break
            r, _, _ = select.select([p.stdout], [], [], min(left, 1.0))
            if not r:
                if p.poll() is not None:
                    break
                continue
            chunk = os.read(p.stdout.fileno(), 4096)
            if not chunk:
                break
            log.write(chunk)
            log.flush()
            buf += chunk
            for line in chunk.decode(errors="replace").splitlines():
                if re.match(r"^IT-\d+ ", line) or "expectation failed" in line:
                    print(line, flush=True)
            m = MARKER.search(buf.decode(errors="replace"))
            if m:
                result = m
                break
    if p.poll() is None:
        try:
            os.killpg(p.pid, signal.SIGTERM)
            p.wait(timeout=5)
        except (ProcessLookupError, subprocess.TimeoutExpired):
            os.killpg(p.pid, signal.SIGKILL)
    elapsed = time.monotonic() - start
    if result is None:
        why = "timeout" if elapsed >= a.timeout else "image ended"
        print(f"run_image: no termination marker ({why} after {elapsed:.1f} s), see {a.log}")
        return 2 if why == "timeout" else 3
    print(f"run_image: {result.group(1)} {result.group(2)}/{result.group(3)} "
          f"in {elapsed:.1f} s ({' '.join(cmd[:1])})")
    return 0 if result.group(1) == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
