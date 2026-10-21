#!/usr/bin/env python3
"""Flight build integration test: the fault injection service is absent.

Starts the FLIGHT build, sends every TC(200,x) subtype the test build
understands, and checks that each one is answered with TM(1,2) failure
code 7 (service not implemented) and nothing else: no acceptance, no
completion, no change of behaviour. A TC(17,1) before and after proves
the link and the command handler are alive, so the rejections are not
an artefact of a dead spacecraft.

    from app/:
      python3 ../lab-5.4-fault-injection/ground/check_flight.py \\
          --spawn ./lab5-flight.host
      python3 ../lab-5.4-fault-injection/ground/check_flight.py --pty \\
          --spawn "sparc-rtems6-sis -leon3 -rt -r lab5-flight.exe"
"""

import argparse
import os
import struct

import fdir
from campaign import is_event, is_hk
from link import Checker, Link

FAIL_SERVICE = 7

INJECTIONS = [
    ("TC(200,1) parameter override", fdir.tc_inject_param("T_BAT", 4800, 0)),
    ("TC(200,2) task hang", fdir.tc_inject_task_hang("ACS_CTRL", 5)),
    ("TC(200,3) supervisor hang", fdir.tc_inject_supervisor_hang()),
    ("TC(200,4) clear", fdir.tc_inject_clear()),
    ("TC(200,99) unknown subtype", (200, 99, b"")),
]


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    Link.add_arguments(p)
    a = p.parse_args()
    if os.path.exists(a.nv_file):
        os.remove(a.nv_file)

    link = Link(a)
    check = Checker(link)

    def replies_to(tc, timeout=3.0):
        """Every service 1 report carrying this TC's request ID."""
        got = []
        for tm in link.collect(timeout):
            if tm.service == 1 and tm.data[:4] == tc[:4]:
                got.append(tm)
        return got

    boot = link.wait_for(is_event(fdir.EVT_BOOT), 15)
    check.check(boot is not None, "flight build booted")
    link.wait_for(is_hk(1), 5)

    tc = link.send((17, 1, b""), "ping before")
    r = replies_to(tc)
    check.check([t.subtype for t in r] == [1, 7],
                "command handler alive: TC(17,1) accepted and completed")

    for name, cmd in INJECTIONS:
        tc = link.send(cmd, name)
        r = replies_to(tc)
        kinds = [t.subtype for t in r]
        code = r[0].data[4] if r and len(r[0].data) > 4 else None
        check.check(kinds == [2] and code == FAIL_SERVICE,
                    f"{name}: only TM(1,2) with code {FAIL_SERVICE} "
                    f"SERVICE (got TM(1,{kinds}) code {code})")

    # The rejected injections had no effect: T_BAT is still the sensor
    # value, every task is alive, and the vehicle is still NOMINAL.
    tms = link.collect(6)
    hk = [fdir.decode_hk(t) for t in tms if t.kind == (3, 25)]
    ev = [fdir.decode_event(t) for t in tms if t.service == 5]
    check.check(hk and hk[-1]["mode"] == 0 and hk[-1]["tbat"] < 45.0
                and sum(hk[-1]["restarts"]) == 0,
                "no effect: NOMINAL, T_BAT nominal, no restart")
    check.check(not ev, f"no event raised ({len(ev)})")

    tc = link.send((17, 1, b""), "ping after")
    r = replies_to(tc)
    check.check([t.subtype for t in r] == [1, 7], "still alive afterwards")

    check.check(not link.errors, "every TM line passed the ground CRC check")
    check.finish()


if __name__ == "__main__":
    main()
