#!/bin/sh
# Instruction count estimate of the security operations (see est_ops.c).
set -e
cd "$(dirname "$0")"
R=../..
INC="-I$R/src/core -I$R/src/security -I$R/src/third_party/tinycrypt/include"
TC=$R/src/third_party/tinycrypt/source
L42=$R/src/core
gcc -O2 -g $INC -o ${TMPDIR:-/tmp}/est_ops est_ops.c $R/src/security/tc_auth.c \
    $R/src/security/keystore.c $TC/sha256.c $TC/hmac.c $TC/aes_encrypt.c \
    $TC/ccm_mode.c $TC/utils.c $L42/crc16.c $L42/pus.c $L42/tc_handler.c \
    $L42/sp.c
valgrind --tool=callgrind --callgrind-out-file=${TMPDIR:-/tmp}/est_ops.cg ${TMPDIR:-/tmp}/est_ops >/dev/null 2>&1
callgrind_annotate --inclusive=yes ${TMPDIR:-/tmp}/est_ops.cg 2>/dev/null | \
    grep -E ":(work_loop|op_plain|op_accept|op_reject|op_scrub|op_load) " | \
    python3 -c '
import re, sys
ir = {}
for line in sys.stdin:
    m = re.match(r"\s*([\d,]+)\s.*:(work_loop|op_\w+)\s", line)
    if m:
        ir[m.group(2)] = int(m.group(1).replace(",", ""))
per_iter_us = 18957.0 / 43000          # Session 4 SIS: ACS_WORK
loop_ir = ir["work_loop"] / 43000.0
print(f"calibration: work loop {loop_ir:.1f} instr/iteration = {per_iter_us:.3f} us on SIS"
      f" -> {per_iter_us / loop_ir * 1000:.1f} ns per x86 instruction")
for k in ("op_plain", "op_accept", "op_reject", "op_scrub", "op_load"):
    n = ir[k] / 50.0
    print(f"{k[3:]:7s} {n:9.0f} instr/op  ~ {n * per_iter_us / loop_ir:8.1f} us on LEON3 (estimate)")
'
