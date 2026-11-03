#!/bin/sh
# Lab 8.3 - start Yamcs 5.13.7 with the Session 8 configuration.
#
#   ./run-yamcs.sh                 # web interface: http://localhost:8090/
#
# Needs Java 17 or later. Downloads the Yamcs release once (about 80 MB) into
# .run/ unless YAMCS_HOME points to an extracted yamcs-5.13.7. Start the
# spacecraft and the bridge in another terminal: ./run-bridge.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
VER=5.13.7
RUN="$HERE/.run"
mkdir -p "$RUN"
if [ -z "${YAMCS_HOME:-}" ]; then
    YAMCS_HOME="$RUN/yamcs-$VER"
    if [ ! -x "$YAMCS_HOME/bin/yamcsd" ]; then
        URL=https://github.com/yamcs/yamcs/releases/download/yamcs-$VER/yamcs-$VER-linux-x86_64.tar.gz
        echo "downloading $URL"
        curl -fL "$URL" -o "$RUN/yamcs.tar.gz"
        tar -xzf "$RUN/yamcs.tar.gz" -C "$RUN"
        rm -f "$RUN/yamcs.tar.gz"
    fi
fi
command -v java >/dev/null || { echo "Java 17+ is needed (sudo apt install openjdk-17-jre-headless)"; exit 2; }

# A Yamcs home made of the release's bin/ and lib/ and this lab's etc/ and mdb/.
H="$RUN/home"
mkdir -p "$H"
for d in bin lib; do ln -sfn "$YAMCS_HOME/$d" "$H/$d"; done
ln -sfn "$HERE/yamcs/etc" "$H/etc"
ln -sfn "$HERE/yamcs/mdb" "$H/mdb"
mkdir -p "$H/displays"
mkdir -p "$H/stacks"
cp "$HERE/displays/"* "$H/displays/" 2>/dev/null || true
cp "$HERE/procedures/"*.ycs "$H/stacks/" 2>/dev/null || true
python3 "$HERE/tools/gen_mdb.py" >/dev/null
exec "$H/bin/yamcsd" --no-color "$@"
