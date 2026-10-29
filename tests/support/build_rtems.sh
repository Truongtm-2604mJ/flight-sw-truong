#!/bin/sh
# Build the RTEMS 6.1 SPARC toolchain (with SIS) and the LEON3 BSP into
# $RTEMS_PREFIX, from the pinned commits in $RSB_COMMIT and $RTEMS_COMMIT.
# CI runs it only on a cache miss; it takes one to three hours.
set -eu
: "${RTEMS_PREFIX:?}" "${RSB_COMMIT:?}" "${RTEMS_COMMIT:?}"
work=$(mktemp -d)
fetch() {   # repository URL, commit, directory
    git init -q "$3" && git -C "$3" fetch -q --depth 1 "$1" "$2" && git -C "$3" checkout -q FETCH_HEAD
}
fetch https://github.com/RTEMS/rtems-source-builder.git "$RSB_COMMIT" "$work/rsb"
( cd "$work/rsb/rtems" && ../source-builder/sb-set-builder --prefix="$RTEMS_PREFIX" \
      --log="$work/rsb.log" 6/rtems-sparc ) || { tail -n 100 "$work/rsb.log"; exit 1; }
fetch https://github.com/RTEMS/rtems.git "$RTEMS_COMMIT" "$work/rtems"
cd "$work/rtems"
printf '[sparc/leon3]\n' > config.ini
PATH="$RTEMS_PREFIX/bin:$PATH" ./waf configure --prefix="$RTEMS_PREFIX"
PATH="$RTEMS_PREFIX/bin:$PATH" ./waf
PATH="$RTEMS_PREFIX/bin:$PATH" ./waf install
rm -rf "$work"
ls "$RTEMS_PREFIX/bin/sparc-rtems6-gcc" "$RTEMS_PREFIX/bin/sparc-rtems6-sis"
