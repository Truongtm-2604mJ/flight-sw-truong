#!/usr/bin/env python3
"""Lab 4.2 ground station: service 17 round trip with verification.

Runs the four acceptance criteria of the lab against the spacecraft and
exits non zero if any of them fails.

    python3 gs_lab42.py --spawn ../lab4-2.host          (host simulation)
    python3 gs_lab42.py --spawn "sis -leon3 -r ../lab4-2.exe"
    python3 gs_lab42.py --tcp localhost:5555            (QEMU serial)
"""

import argparse
import time

import pus
from link import Checker, Link


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    Link.add_arguments(parser)
    parser.add_argument("--boot-wait", type=float, default=1.0,
                        help="seconds to wait for the application to start")
    parser.add_argument("--quiet", type=float, default=0.6,
                        help="seconds to wait for the replies to one TC; "
                             "raise it if the simulator runs slower than "
                             "real time")
    args = parser.parse_args()
    QUIET = args.quiet

    link = Link.from_args(args)
    check = Checker(link)
    cmd = pus.APID_CMD

    def kinds(tms):
        return [tm.kind for tm in tms]

    link.collect(args.boot_wait)          # show the boot banner

    # -- 1. valid test command ------------------------------------------------
    link.log("# 1. Valid TC(17,1): expect acceptance, test report, completion")
    tc = link.send_tc(17, 1, note="are-you-alive")
    got = link.collect(QUIET, cmd)
    check.check(kinds(got) == [(1, 1), (17, 2), (1, 7)],
                f"TM order is (1,1) (17,2) (1,7): got {kinds(got)}")
    check.check(all(pus.decode_verification(t)["request_id"] == tc[:4]
                    for t in got if t.service == 1),
                f"verification reports carry request ID {tc[:4].hex().upper()}")
    check.check(all(t.dest_id == 1 for t in got),
                "reports are addressed to source ID 1")

    # -- 2. corrupted checksum ------------------------------------------------
    link.log("# 2. Corrupted checksum: expect acceptance failure, no execution")
    good = pus.build_tc(link.next_tc_seq(), 17, 1)
    bad = good[:-1] + bytes([good[-1] ^ 0x01])
    link.send_raw(bad, "TC(17,1) with the last CRC bit flipped")
    got = link.collect(QUIET, cmd)
    check.check(kinds(got) == [(1, 2)],
                f"only an acceptance failure TM(1,2): got {kinds(got)}")
    crc_code = pus.decode_verification(got[0]).get("code") if got else None
    check.check(crc_code == 3, f"failure code is 3 CHECKSUM: got {crc_code}")
    check.check((17, 2) not in kinds(got), "no test report: not executed")

    # -- 3. unimplemented service ----------------------------------------------
    link.log("# 3. Unimplemented service 8: expect a different failure code")
    link.send_tc(8, 1, note="service not implemented onboard")
    got = link.collect(QUIET, cmd)
    check.check(kinds(got) == [(1, 2)],
                f"only an acceptance failure TM(1,2): got {kinds(got)}")
    svc_code = pus.decode_verification(got[0]).get("code") if got else None
    check.check(svc_code == 7, f"failure code is 7 SERVICE: got {svc_code}")
    check.check(svc_code != crc_code,
                "failure code differs from the checksum failure code")

    # -- extra: the remaining validation checks --------------------------------
    link.log("# Extra: other failure codes in the validation sequence")
    extra = [
        ("unknown subtype", dict(service=17, subtype=99), 8),
        ("unexpected application data",
         dict(service=17, subtype=1, app_data=b"\x00"), 9),
        ("wrong APID", dict(service=17, subtype=1, apid=0x011), 5),
    ]
    for name, kwargs, expected in extra:
        service, subtype = kwargs.pop("service"), kwargs.pop("subtype")
        app = kwargs.pop("app_data", b"")
        link.send_tc(service, subtype, app, note=name, **kwargs)
        got = link.collect(QUIET, cmd)
        code = pus.decode_verification(got[0]).get("code") if got else None
        check.check(kinds(got) == [(1, 2)] and code == expected,
                    f"{name}: TM(1,2) with code {expected}: got code {code}")

    truncated = pus.build_tc(link.next_tc_seq(), 17, 1)[:-3]
    link.send_raw(truncated, "TC(17,1) with 3 octets missing")
    got = link.collect(QUIET, cmd)
    code = pus.decode_verification(got[0]).get("code") if got else None
    check.check(kinds(got) == [(1, 2)] and code == 2,
                f"truncated packet: TM(1,2) with code 2: got code {code}")

    # -- the spacecraft still works after all that ------------------------------
    link.log("# Valid TC(17,1) again, all four acknowledgement flags set")
    link.send_tc(17, 1, note="ack flags 1111", ack=0xF)
    got = link.collect(QUIET, cmd)
    check.check(kinds(got) == [(1, 1), (1, 3), (17, 2), (1, 7)],
                f"TM order is (1,1) (1,3) (17,2) (1,7): got {kinds(got)}")

    # -- 4. sequence counts ------------------------------------------------------
    link.log("# 4. Sequence counts across the whole exchange")
    check.check_sequence(cmd)
    check.check(not link.errors, "every TM line passed the ground CRC check")

    time.sleep(0.1)
    check.finish()


if __name__ == "__main__":
    main()
