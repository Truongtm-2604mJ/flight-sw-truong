#!/bin/sh
# LOTUS-1 - target evidence on RTEMS 6 / LEON3 (SIS). About 25 minutes.
#
#   RTEMS_PREFIX=$HOME/quick-start/rtems/6 make sis      (from the repository root)
#   or: RTEMS_PREFIX=... sh scripts/run-sis.sh [step...]
#
# Steps (all by default, or name some: timing demo security validation):
#   timing      1. build/rtems/sec-timing.exe    cost of each security operation
#               2. build/rtems/lotus1-timing.exe integrated flight build, 60 s,
#                                                rate monotonic statistics
#               3. tools/timing/budget.py        the timing budget from 1 and 2
#                  -> evidence/timing/sis-sec-timing.log, sis-rm.log, budget-sis.md
#   demo        4. demo/scenario.py              the 7 step mission scenario on SIS
#                  -> evidence/demo/scenario-sis.log, scenario-sis.json
#   security    5. tests/security/test_auth_system.py  Session 8 security system tests
#                  -> evidence/test-runs/security-system-sis.log ([8.1-6] fails on SIS: NCR-04)
#   validation  6. make -C tests target          integration test image on SIS, then
#                                                the validation campaign on SIS
#                  -> evidence/validation/results-sis/
#
# SIS has no non volatile memory: tests that need state kept over a reset are
# reported as not applicable there (--nv-volatile); they pass on the host.
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)
if [ -n "${RTEMS_PREFIX:-}" ] && [ -x "$RTEMS_PREFIX/bin/sparc-rtems6-sis" ]; then
    SIS=${SIS:-$RTEMS_PREFIX/bin/sparc-rtems6-sis}
else
    SIS=${SIS:-sparc-rtems6-sis}
fi
command -v "$SIS" >/dev/null 2>&1 || { echo "SIS not found: $SIS (set RTEMS_PREFIX or SIS)"; exit 2; }
STEPS=${*:-timing demo security validation}
E=evidence
mkdir -p $E/timing $E/demo

echo "== build (RTEMS)"
if [ -z "${SKIP_BUILD:-}" ]; then
    make -s rtems ${RTEMS_PREFIX:+RTEMS_PREFIX="$RTEMS_PREFIX"} || exit 2
fi
rc=0
for s in $STEPS; do
    case $s in
    timing)
        echo "== security operation timing (about 1 min)"
        python3 tools/pty_run.py --until "Timing finished" --timeout 900 \
            --log $E/timing/sis-sec-timing.log -- $SIS -leon3 -r build/rtems/sec-timing.exe
        grep -E "mean|accepted|rejections" $E/timing/sis-sec-timing.log
        echo "== integrated flight build, 60 s simulated, rate monotonic statistics (a few min)"
        python3 tools/pty_run.py --until "Simulation finished" --timeout 1500 \
            --log $E/timing/sis-rm.log -- $SIS -leon3 -r build/rtems/lotus1-timing.exe
        echo "== timing budget"
        python3 tools/timing/budget.py --sec-timing $E/timing/sis-sec-timing.log \
            --rm $E/timing/sis-rm.log --out $E/timing/budget-sis.md || rc=1
        ;;
    demo)
        echo "== mission scenario on SIS (about 3 min)"
        rm -f $E/demo/nv-sis.bin $E/demo/nv-sis.bin.sec $E/demo/nv-sis.bin.ground.json
        python3 demo/scenario.py --pty --nv-file $E/demo/nv-sis.bin \
            --spawn "$SIS -leon3 -rt -r build/rtems/lotus1-test.exe" \
            --log $E/demo/scenario-sis.log --json $E/demo/scenario-sis.json || rc=1
        rm -f $E/demo/nv-sis.bin $E/demo/nv-sis.bin.sec $E/demo/nv-sis.bin.ground.json
        ;;
    security)
        echo "== security system tests on SIS (about 3 min)"
        mkdir -p $E/test-runs
        rm -f /tmp/lotus1_system_nv.bin*
        python3 tests/security/test_auth_system.py --pty \
            --spawn "$SIS -leon3 -rt -r build/rtems/lotus1-test.exe" \
            --log $E/test-runs/security-system-sis.log > /dev/null 2>&1
        grep -E "FAIL|result" $E/test-runs/security-system-sis.log | tail -4
        ;;
    validation)
        echo "== integration image and validation campaign on SIS (about 20 min)"
        make -C tests target SIS="$SIS" ${RTEMS_PREFIX:+RTEMS_PREFIX="$RTEMS_PREFIX"} || rc=1
        ;;
    *) echo "unknown step: $s (timing, demo, security, validation)"; exit 2 ;;
    esac
done
echo "== done (rc=$rc): commit evidence/timing, evidence/demo and evidence/validation/results-sis"
exit $rc
