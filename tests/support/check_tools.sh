#!/bin/sh
# Fails unless every tool is exactly the pinned version (Lab 7.3: no floating
# versions). Run first in every CI job and by `make tools` locally, so a run's
# evidence always names the tools that produced it.
set -u
fail=0
want() {   # name, command printing the version, expected substring
    got=$(sh -c "$2" 2>/dev/null | head -n 1)
    case "$got" in
        *"$3"*) printf '  ok    %-10s %s\n' "$1" "$got" ;;
        *)      printf '  WRONG %-10s %s (want %s)\n' "$1" "${got:-missing}" "$3"; fail=1 ;;
    esac
}
echo "pinned tools:"
want gcc       "gcc --version"            "13.3.0"
want gcov      "gcov --version"           "13.3.0"
want clang     "clang-18 --version || clang --version" "18.1.3"
want llvm-cov  "llvm-cov-18 --version | grep -i version || llvm-cov --version | grep -i version" "18.1.3"
want cppcheck  "cppcheck --version"       "Cppcheck 2.13.0"
want gcovr     "gcovr --version"          "gcovr 7.2"
want python    "python3 --version"        "Python 3."
want pyyaml    "python3 -c 'import yaml; print(yaml.__version__)'" "6."
# The RTEMS cross toolchain and SIS are pinned by the RTEMS Source Builder
# commit that built them (.github/workflows/nightly-target.yml); print them.
if [ -n "${RTEMS_PREFIX:-}" ]; then
    want sparc-gcc "$RTEMS_PREFIX/bin/sparc-rtems6-gcc --version" "sparc-rtems6-gcc"
    [ -x "$RTEMS_PREFIX/bin/sparc-rtems6-sis" ] && echo "  ok    sis        $RTEMS_PREFIX/bin/sparc-rtems6-sis" \
        || { echo "  WRONG sis        missing"; fail=1; }
fi
exit $fail
