#!/bin/sh
# LOTUS-1: every check, the target evidence and the demo video, unattended.
#
#   nohup sh scripts/run-all.sh > run-all.log 2>&1 &      # leave it running, about 60 min
#   tail -f run-all.log                                   # watch it (Ctrl-C stops watching only)
#
# Environment (all optional):
#   RTEMS_PREFIX=$HOME/Work/01.Code/rtems/rtems/6   SIS steps (skipped if SIS is not found)
#   SKIP_SIS=1  SKIP_VIDEO=1  SKIP_VALIDATION=1     leave a part out
#   COMMIT=1                                        commit and push the regenerated evidence at the end
#
# Writes run-all-summary.txt: one line per step, PASS or FAIL, then the key numbers.
# Steps run one after the other, so the timing measurements are not disturbed.
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)
S=$ROOT/run-all-summary.txt
L=$ROOT/build/run-all
mkdir -p "$L"
: > "$S"
T0=$(date +%s)
note() { echo "$*" | tee -a "$S"; }
step() {   # step NAME command...: run it, log it, record PASS/FAIL
    n=$1; shift
    echo "== $n ($(date +%H:%M:%S))"
    if "$@" > "$L/$n.log" 2>&1; then note "PASS  $n"; else note "FAIL  $n  (build/run-all/$n.log)"; fi
}

note "LOTUS-1 run-all, $(date), commit $(git rev-parse --short HEAD)"
git pull -q --ff-only || note "note: git pull failed (offline or local changes), running on the local tree"

# ---- host --------------------------------------------------------------------------------
step build            make -s host
step model            make -s check-generated
step mdb              make -s mdb-check
step warnings         make -s -C tests warnings
step unit             make -s -C tests unit
step integration      make -s -C tests integration
step security         make -s -C tests security
step scenario         make -s scenario
[ -n "${SKIP_VALIDATION:-}" ] || step validation make -s -C tests validation
step matrix           make -s -C tests evidence matrix

# ---- SIS (RTEMS 6 / LEON3) ------------------------------------------------------------------
if [ -z "${SKIP_SIS:-}" ]; then
    if [ -n "${RTEMS_PREFIX:-}" ] && [ -x "$RTEMS_PREFIX/bin/sparc-rtems6-sis" ] || command -v sparc-rtems6-sis >/dev/null; then
        step sis      make sis ${RTEMS_PREFIX:+RTEMS_PREFIX="$RTEMS_PREFIX"}
        make -s -C tests matrix > "$L/matrix.log" 2>&1     # again, with the SIS results
    else
        note "SKIP  sis  (set RTEMS_PREFIX to the RTEMS 6 install)"
    fi
fi

# ---- demo video through Yamcs ---------------------------------------------------------------------
if [ -z "${SKIP_VIDEO:-}" ]; then
    if command -v java >/dev/null && command -v ffmpeg >/dev/null && python3 -c "import playwright" 2>/dev/null; then
        rm -rf mcs/.run/home/data mcs/.run/home/cache
        sh mcs/run-yamcs.sh > "$L/yamcs.log" 2>&1 &
        YPID=$!
        i=0; until grep -q "Website deployed" "$L/yamcs.log" 2>/dev/null || [ $i -ge 120 ]; do sleep 2; i=$((i+2)); done
        step video    python3 demo/record_demo.py --out evidence/demo/lotus1-demo.mp4
        pkill -f "bridge/sc""_bridge.py" 2>/dev/null
        kill $YPID 2>/dev/null; sleep 2; kill $(pgrep -f "org.yamcs.YamcsServer") 2>/dev/null
    else
        note "SKIP  video  (needs java 17+, ffmpeg and Python Playwright:"
        note "             sudo apt install openjdk-17-jre-headless ffmpeg;"
        note "             pip install --user playwright && python3 -m playwright install chromium)"
    fi
fi

# ---- report ---------------------------------------------------------------------------------------
if command -v pandoc >/dev/null && python3 -c "import playwright" 2>/dev/null; then
    step report   sh docs/report/build.sh
else
    note "SKIP  report  (needs pandoc and Python Playwright; the committed PDF stays as it is)"
fi

# ---- summary ---------------------------------------------------------------------------------------
note ""
note "Key numbers:"
grep -h "campaign:" build/run-all/validation.log build/run-all/sis.log 2>/dev/null | sed 's/^/  /' | tee -a "$S" >/dev/null
grep -h "# result" evidence/test-runs/security-system-*.log 2>/dev/null | sed 's/^/  security: /' >> "$S"
grep -h "Verdict" evidence/timing/budget-sis.md 2>/dev/null | sed 's/^/  SIS timing: /' >> "$S"
grep -h "demo checks" evidence/demo/yamcs-demo-run.txt 2>/dev/null | sed 's/^/  Yamcs demo: /' >> "$S"
grep -h "build_matrix:" build/run-all/matrix.log 2>/dev/null | tail -1 | sed 's/^/  /' >> "$S"
[ -f evidence/demo/lotus1-demo.mp4 ] && note "  video: evidence/demo/lotus1-demo.mp4 (upload it; it is not committed)"
note "Total: $(( ($(date +%s) - T0) / 60 )) min.  Logs: build/run-all/"

if [ -n "${COMMIT:-}" ]; then
    git add evidence docs/report
    git reset -q evidence/demo/lotus1-demo.mp4 2>/dev/null
    git commit -q -m "run-all: regenerated evidence ($(date +%Y-%m-%d))" && git push -q && note "committed and pushed: $(git rev-parse --short HEAD)"
fi
grep -q "^FAIL" "$S" && exit 1 || exit 0
