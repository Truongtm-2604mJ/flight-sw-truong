#!/usr/bin/env python3
"""MC/DC evidence (Lab 7.2), two independent ways:

1. Tool measurement: the unit suites are rebuilt with clang source based
   coverage and MC/DC instrumentation (-fcoverage-mcdc, clang/LLVM 18) and
   llvm-cov reports, per flight module, the MC/DC of every decision with
   two or more conditions.
2. Mutation check of the hand built MC/DC tables: each condition of the
   decisions MC-1..MC-5 is replaced by a constant, and the unit suite of
   that module is run. A condition whose mutation fails at least one test
   has been shown to matter; one whose mutation fails nothing is either
   untested or (MC-5, C) has no independent effect by construction.

    python3 support/mcdc_clang.py --root .. --out ../labs/session-07/coverage

Needs clang, llvm-profdata, llvm-cov 18 and the compiler-rt profile runtime
(Ubuntu: libclang-rt-18-dev).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.dirname(HERE)


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--work", default=os.path.join(TESTS, "build", "mcdc"))
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    L = os.path.join(root, "labs")
    S4, S5, S6 = (os.path.join(L, s) for s in ("session-04", "session-05", "session-06"))
    gen = os.path.join(S6, "lab-6.2-generation", "generated")
    unity = os.path.join(HERE, "unity")
    inc = [f"-I{unity}", f"-I{S4}/lab-4.1-space-packet", f"-I{S4}/lab-4.2-pus-service17",
           f"-I{S5}/lab-5.1-monitoring", f"-I{S5}/lab-5.2-liveness",
           f"-I{S5}/lab-5.3-safe-mode", f"-I{gen}"]
    crc = f"{S4}/lab-4.2-pus-service17/crc16.c"
    suites = {
        "test_sp":       [f"{S4}/lab-4.1-space-packet/sp.c"],
        "test_pus":      [crc, f"{S4}/lab-4.2-pus-service17/pus.c",
                          f"{S4}/lab-4.2-pus-service17/tc_handler.c", f"{S4}/lab-4.1-space-packet/sp.c"],
        "test_monitor":  [f"{S5}/lab-5.1-monitoring/monitor.c"],
        "test_liveness": [f"{S5}/lab-5.2-liveness/supervisor.c", f"{S5}/lab-5.2-liveness/watchdog.c",
                          f"{S5}/lab-5.2-liveness/nvstate.c", crc],
        "test_schedule": [f"{S5}/lab-5.3-safe-mode/schedule.c"],
        "test_mode_sm":  [f"{gen}/mode_sm.c"],
    }
    flags = ["-std=c99", "-O0", "-fprofile-instr-generate", "-fcoverage-mapping",
             "-fcoverage-mcdc", "-DMODE_SM_WITH_NAMES"]
    shutil.rmtree(a.work, ignore_errors=True)
    os.makedirs(a.work)
    os.makedirs(a.out, exist_ok=True)

    # ---- 1. tool measurement ------------------------------------------------
    bins, profs, objs = [], [], []
    for name, srcs in suites.items():
        exe = os.path.join(a.work, name)
        r = sh(["clang", *flags, *inc, "-o", exe, os.path.join(TESTS, "unit", name + ".c"),
                *srcs, os.path.join(unity, "unity.c")])
        if r.returncode:
            sys.exit(f"build {name} failed:\n{r.stderr}")
        raw = exe + ".profraw"
        r = sh([exe], env={**os.environ, "LLVM_PROFILE_FILE": raw})
        if r.returncode:
            sys.exit(f"{name} failed under instrumentation:\n{r.stdout}")
        bins.append(exe)
        profs.append(raw)
    prof = os.path.join(a.work, "all.profdata")
    sh(["llvm-profdata", "merge", "-o", prof, *profs]).check_returncode()
    flight = sorted({s for v in suites.values() for s in v})
    objargs = [bins[0]] + [x for b in bins[1:] for x in ("-object", b)]
    rep = sh(["llvm-cov", "report", *objargs, f"-instr-profile={prof}", "--show-mcdc-summary",
              "--show-region-summary=false", "--show-instantiation-summary=false", *flight])
    summary = rep.stdout.replace(root + "/", "")
    show = sh(["llvm-cov", "show", *objargs, f"-instr-profile={prof}", "--show-mcdc",
               "--show-line-counts", *flight]).stdout.replace(root + "/", "")
    with open(os.path.join(a.out, "mcdc-clang-summary.txt"), "w") as f:
        f.write("# clang 18 source based coverage with MC/DC, unit level, flight modules\n")
        f.write(f"# {sh(['clang', '--version']).stdout.splitlines()[0]}\n\n")
        f.write(summary)
    with open(os.path.join(a.out, "mcdc-clang-detail.txt"), "w") as f:
        f.write(show)
    # Decisions not at 100 %: extract each decision report with its file and
    # source line.
    gaps = []
    file_at = [(m.start(), m.group(1)) for m in re.finditer(r"^(\S+\.c):$", show, re.M)]
    for m in re.finditer(r"\|---> MC/DC Decision Region \((\d+):\d+\).*?"
                         r"MC/DC Coverage for Decision: ([\d.]+)%", show, re.S):
        if float(m.group(2)) < 100.0:
            fname = [f for pos, f in file_at if pos < m.start()]
            line = int(m.group(1))
            src_line = re.findall(r"^\s*%d\|\s*[\d.kM]*\|(.*)$" % line, show[:m.start()], re.M)
            gaps.append((fname[-1] if fname else "?", line,
                         src_line[-1].strip() if src_line else "", m.group(0)))

    # ---- 2. mutation check ---------------------------------------------------
    mut_dir = os.path.join(a.work, "mut")
    os.makedirs(mut_dir)
    mon = f"{S5}/lab-5.1-monitoring/monitor.c"
    sup = f"{S5}/lab-5.2-liveness/supervisor.c"
    msm = f"{gen}/mode_sm.c"
    T02 = r"(pending & BIT(MODE_SM_EV_FAULT_CRITICAL)) != 0U && (!in->fdir_inhibited)) {   /* T02"
    T05 = r"(pending & BIT(MODE_SM_EV_FAULT_CRITICAL)) != 0U && in->fdir_inhibited) {   /* T05"
    mutations = [
        ("MC-1", "A", mon, "value >= e->limit_low_clear && value <= e->limit_high_clear",
         "1 && value <= e->limit_high_clear", "test_monitor"),
        ("MC-1", "B", mon, "value >= e->limit_low_clear && value <= e->limit_high_clear",
         "value >= e->limit_low_clear && 1", "test_monitor"),
        ("MC-2", "A", mon, "side != e->side && e->violation_count > 0u",
         "1 && e->violation_count > 0u", "test_monitor"),
        ("MC-2", "B", mon, "side != e->side && e->violation_count > 0u",
         "side != e->side && 1", "test_monitor"),
        ("MC-3", "A", sup, "s->ops->actions_inhibited != NULL &&",
         "1 &&", "test_liveness"),
        ("MC-3", "B", sup, "&&\n                     s->ops->actions_inhibited(s->arg))",
         "&& 1)", "test_liveness"),
        ("MC-4", "A", msm, T02, T02.replace("(pending & BIT(MODE_SM_EV_FAULT_CRITICAL)) != 0U", "1"),
         "test_mode_sm"),
        ("MC-4", "B", msm, T02, T02.replace("(!in->fdir_inhibited)", "1"), "test_mode_sm"),
        ("MC-5", "A", msm, T05, T05.replace("(pending & BIT(MODE_SM_EV_FAULT_CRITICAL)) != 0U", "1"),
         "test_mode_sm"),
        ("MC-5", "C", msm, T05, T05.replace("&& in->fdir_inhibited)", "&& 1)"), "test_mode_sm"),
    ]
    rows = []
    for dec, cond, src, old, new, suite in mutations:
        text = open(src).read()
        if text.count(old) != 1:
            sys.exit(f"{dec} {cond}: mutation site not found exactly once in {src}")
        mpath = os.path.join(mut_dir, os.path.basename(src))
        with open(mpath, "w") as f:
            f.write(text.replace(old, new))
        srcs = [mpath if s == src else s for s in suites[suite]]
        exe = os.path.join(mut_dir, suite)
        r = sh(["cc", "-std=c99", "-O0", "-DMODE_SM_WITH_NAMES", *inc, "-o", exe,
                os.path.join(TESTS, "unit", suite + ".c"), *srcs, os.path.join(unity, "unity.c")])
        if r.returncode:
            sys.exit(f"mutant {dec} {cond} does not build:\n{r.stderr}")
        r = sh([exe])
        failed = re.findall(r":(test_\w+):FAIL", r.stdout)
        if r.returncode < 0:
            verdict, killers = "killed (crash)", f"suite crashed (signal {-r.returncode})"
        elif failed:
            verdict = "killed"
            mc = [t for t in failed if t.startswith("test_mcdc_")]
            killers = ", ".join(f"`{t}`" for t in (mc or failed)[:3])
            if len(failed) > 3:
                killers += f" (+{len(failed) - 3} more)"
        else:
            verdict, killers = "SURVIVED", "no test fails"
        rows.append((dec, cond, os.path.relpath(src, root), " ".join(old.split()),
                     " ".join(new.split()), verdict, killers))

    with open(os.path.join(a.out, "mcdc-mutation.md"), "w") as f:
        f.write("# MC/DC mutation check (generated by `tests/support/mcdc_clang.py`)\n\n")
        f.write("Each condition of the hand built MC/DC decisions is replaced by the constant 1 "
                "(always true), and the module's unit suite is run against the mutant.\n\n")
        f.write("| Decision | Condition | File | Original | Mutant | Result | Detected by |\n")
        f.write("|---|---|---|---|---|---|---|\n")
        for r in rows:
            f.write(f"| {r[0]} | {r[1]} | `{r[2]}` | `{r[3]}` | `{r[4]}` | {r[5]} | {r[6]} |\n")
        surv = [r for r in rows if r[5] == "SURVIVED"]
        f.write(f"\n{len(rows) - len(surv)} of {len(rows)} mutants detected.")
        if surv:
            f.write(" Surviving: " + ", ".join(f"{r[0]} {r[1]}" for r in surv) +
                    " (see mcdc.md for the argument).")
        f.write("\n")
        f.write("\n## Decisions below 100 % MC/DC in the clang measurement\n\n")
        if gaps:
            for fname, line, code, block in gaps:
                f.write(f"### `{fname}` line {line}\n\n`{code}`\n\n```\n|{block}\n```\n\n")
        else:
            f.write("None.\n")
    print(summary.splitlines()[-1] if summary else rep.stderr)
    print(open(os.path.join(a.out, "mcdc-mutation.md")).read().split("## Decisions")[0])
    print(f"{len(gaps)} decisions below 100 % MC/DC: " +
          ", ".join(f"{os.path.basename(g[0])}:{g[1]}" for g in gaps))


if __name__ == "__main__":
    main()
