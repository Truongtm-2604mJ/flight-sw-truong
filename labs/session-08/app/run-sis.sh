#!/bin/sh
# Session 8 - target evidence on RTEMS 6 / LEON3 (SIS). About 10 minutes.
#
#   cd labs/session-08/app
#   RTEMS_PREFIX=$HOME/Work/01.Code/rtems/rtems/6 ./run-sis.sh
#
#   1. sec-timing.exe     cost of each security operation  -> ../docs/timing/sis-sec-timing.log
#   2. lab8-timing.exe    flight build, 60 s, rate monotonic statistics
#                                                       -> ../docs/timing/sis-rm.log
#   3. budget.py          the timing budget from 1 and 2 -> ../docs/timing/budget-sis.md
#   4. system tests (Labs 8.1, 8.2) on SIS               -> ../lab-8.1-authentication/tests/system-test-sis.log
#      (case [8.1-6], counter kept across a reset, needs non volatile memory:
#       SIS has none, so it is expected to fail there; it passes on the host)
set -u
cd "$(dirname "$0")"
if [ -n "${RTEMS_PREFIX:-}" ] && [ -x "$RTEMS_PREFIX/bin/sparc-rtems6-sis" ]; then
    SIS=${SIS:-$RTEMS_PREFIX/bin/sparc-rtems6-sis}
else
    SIS=${SIS:-sparc-rtems6-sis}
fi
command -v "$SIS" >/dev/null 2>&1 || { echo "SIS not found: $SIS (set RTEMS_PREFIX or SIS)"; exit 2; }
T=../docs/timing
mkdir -p $T

echo "== build"
if [ -z "${SKIP_BUILD:-}" ]; then
    make rtems ${RTEMS_PREFIX:+RTEMS_PREFIX="$RTEMS_PREFIX"} || exit 2
fi
echo "== 1/4 security operation timing (about 1 min)"
python3 pty_run.py --until "Timing finished" --timeout 900 --log $T/sis-sec-timing.log -- \
    $SIS -leon3 -r sec-timing.exe
grep -E "mean|accepted|rejections" $T/sis-sec-timing.log
echo "== 2/4 flight build, 60 s simulated, rate monotonic statistics (a few min)"
python3 pty_run.py --until "Simulation finished" --timeout 1500 --log $T/sis-rm.log -- \
    $SIS -leon3 -r lab8-timing.exe
echo "== 3/4 timing budget"
python3 $T/budget.py --sec-timing $T/sis-sec-timing.log --rm $T/sis-rm.log --out $T/budget-sis.md \
    --report ../docs/security-report.md
echo "== 4/4 system tests on SIS (about 3 min)"
python3 ../lab-8.1-authentication/tests/test_auth_system.py --pty \
    --spawn "$SIS -leon3 -rt -r lab8-test.exe" \
    --log ../lab-8.1-authentication/tests/system-test-sis.log > /dev/null 2>&1
grep -E "FAIL|result" ../lab-8.1-authentication/tests/system-test-sis.log
echo "== done: commit docs/timing/ and lab-8.1-authentication/tests/system-test-sis.log"
