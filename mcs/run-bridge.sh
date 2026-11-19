#!/bin/sh
# LOTUS-1 mission control: start the spacecraft behind the ground security unit.
#
#   ./run-bridge.sh                  host simulation, test build (default)
#   ./run-bridge.sh sis              SIS, test build (RTEMS_PREFIX or SIS set)
#   ./run-bridge.sh fresh [sis]      start from a factory key store (forget NV)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
mkdir -p "$HERE/.run"
if [ "${1:-}" = "fresh" ]; then
    rm -f "$HERE/.run/spacecraft_nv.bin"*
    shift
fi
if [ "${1:-}" = "sis" ]; then
    if [ -n "${RTEMS_PREFIX:-}" ] && [ -x "$RTEMS_PREFIX/bin/sparc-rtems6-sis" ]; then
        SIS=${SIS:-$RTEMS_PREFIX/bin/sparc-rtems6-sis}
    else
        SIS=${SIS:-sparc-rtems6-sis}
    fi
    [ -f "$ROOT/build/rtems/lotus1-test.exe" ] || make -C "$ROOT" build/rtems/lotus1-test.exe
    exec python3 "$HERE/bridge/sc_bridge.py" --pty \
        --spawn "$SIS -leon3 -rt -r $ROOT/build/rtems/lotus1-test.exe" --log "$HERE/.run/bridge.log"
fi
make -s -C "$ROOT" build/host/lotus1-test.host
exec python3 "$HERE/bridge/sc_bridge.py" --spawn "$ROOT/build/host/lotus1-test.host" \
    --log "$HERE/.run/bridge.log"
