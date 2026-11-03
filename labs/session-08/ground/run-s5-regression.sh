#!/bin/sh
# The Session 5 system scenarios, unchanged, against the Session 8 build:
# every TC they send is authenticated by the ground security unit
# (run_secure.py). Results in lab-8.1-authentication/tests/regression/.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
S8=$(cd "$HERE/.." && pwd)
G5=$(cd "$S8/../session-05/lab-5.4-fault-injection/ground" && pwd)
OUT="$S8/lab-8.1-authentication/tests/regression"
PY=${PYTHON:-python3}
mkdir -p "$OUT"
NV=${TMPDIR:-/tmp}/s8_regr
rm -f "$NV"_*
rc=0
cd "$S8/app"
echo "== gs_modes.py";    $PY "$HERE/run_secure.py" "$G5/gs_modes.py" --spawn ./lab8-test.host \
    --nv-file "${NV}_modes.bin" --log "$OUT/gs_modes.log" > /dev/null || rc=1
tail -1 "$OUT/gs_modes.log"
echo "== campaign.py";    $PY "$HERE/run_secure.py" "$G5/campaign.py" --spawn ./lab8-test.host \
    --label host-sim-s8 --results "$OUT/campaign.md" --nv-file "${NV}_c.bin" \
    --log "$OUT/campaign.log" > /dev/null || rc=1
tail -1 "$OUT/campaign.log"
echo "== check_flight.py"; $PY "$HERE/run_secure.py" "$G5/check_flight.py" --spawn ./lab8-flight.host \
    --nv-file "${NV}_f.bin" --log "$OUT/check_flight.log" > /dev/null || rc=1
tail -1 "$OUT/check_flight.log"
exit $rc
