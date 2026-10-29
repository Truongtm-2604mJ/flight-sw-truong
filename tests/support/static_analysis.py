#!/usr/bin/env python3
"""Lab 7.2 - Static analysis campaign and triage.

Runs four layers over every flight source, test build and flight build
(FDIR_TEST_BUILD on and off), on the host simulation headers:

  gcc       GCC 13 at the maximum warning set (tests/Makefile WARN_MAX, -Werror
            removed so that every finding is listed)
  clang     clang 18 -Weverything (every clang warning), for the categories
            GCC does not have
  cppcheck  cppcheck 2.13.0, --enable=all --inconclusive, plus its MISRA C:2012
            addon (rule numbers only; the rule texts are licensed)
  analyzer  clang static analyser (the engine behind scan-build), core,
            security, unix, deadcode and optin.portability checkers
  fanalyzer GCC 13 -fanalyzer, an independent path sensitive analyser

Each finding gets a key (tool, check, file, normalised message). Every key
must have a disposition in
labs/session-07/static-analysis/dispositions.yaml: fixed (then it must no
longer appear), justified (with the argument), or accepted (with the
stated risk). A finding without a disposition is NEW and makes the script
exit 1: that is how CI fails on new warnings.

    python3 support/static_analysis.py --out ../labs/session-07/static-analysis \
        -- <include flags> -- <flight sources>

Writes raw/ (each tool's complete output), findings.json and findings-log.md.
"""
import argparse
import collections
import json
import os
import re
import shutil
import subprocess
import sys

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

GCC_WARN = ("-std=c99 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion "
            "-Wshadow -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -Wswitch-enum "
            "-Wundef -Wdouble-promotion -Wformat=2 -Wnull-dereference -Wduplicated-cond "
            "-Wduplicated-branches -Wlogical-op -Wcast-align=strict -Wjump-misses-init "
            "-Wvla -Wredundant-decls -Wnested-externs -Wold-style-definition "
            "-Wbad-function-cast -Wwrite-strings -Wfloat-equal -Wstack-usage=2048 "
            "-Walloca -Wformat-overflow=2 -Wformat-truncation=2 -Wunused-macros").split()
CLANG_WARN = ["-std=c99", "-O2", "-Weverything"]
FANALYZER = ["-std=c99", "-O2", "-fanalyzer"]
ANALYZER = ["--analyze", "-Xclang", "-analyzer-output=text",
            "-Xclang", "-analyzer-checker=core,security,unix,deadcode,optin.portability"]
BUILDS = [("test", ["-DFDIR_TEST_BUILD"]), ("flight", [])]


def rel(path):
    p = os.path.normpath(os.path.join(os.getcwd(), path)) if not os.path.isabs(path) else path
    return os.path.relpath(p, ROOT)


DIAG = re.compile(r"^(?P<file>[^:\s]+\.[ch]):(?P<line>\d+):(?:(?P<col>\d+):)?\s*"
                  r"(?P<sev>warning|error|style|portability|performance|information|note)"
                  r":\s*(?P<msg>.*?)(?:\s*\[(?P<check>[-\w.,=+]+)\])?\s*$")


def norm_msg(msg):
    """Message without numbers and quoted identifiers that change with edits."""
    m = re.sub(r"'[^']*'", "'X'", msg)
    return re.sub(r"\d+", "N", m)


def parse(tool, text, flight):
    out = []
    for line in text.splitlines():
        m = DIAG.match(line.strip())
        if not m or m.group("sev") == "note":
            continue
        f = rel(m.group("file"))
        if not any(f == rel(s) for s in flight):
            continue          # headers of the host simulation, system headers
        check = m.group("check") or "-"
        out.append(dict(tool=tool, file=f, line=int(m.group("line")), check=check,
                        sev=m.group("sev"), msg=m.group("msg")))
    return out


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.stdout + r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("rest", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    rest = a.rest[1:] if a.rest and a.rest[0] == "--" else a.rest
    split = rest.index("--")
    inc, flight = rest[:split], rest[split + 1:]
    raw = os.path.join(a.out, "raw")
    shutil.rmtree(raw, ignore_errors=True)
    os.makedirs(raw)
    found = []
    versions = {
        "gcc": run(["gcc", "--version"]).splitlines()[0],
        "clang": run(["clang", "--version"]).splitlines()[0],
        "cppcheck": run(["cppcheck", "--version"]).strip(),
    }

    for bname, bflags in BUILDS:
        for tool, flags in (("gcc", GCC_WARN), ("clang", CLANG_WARN), ("analyzer", ANALYZER),
                            ("fanalyzer", FANALYZER)):
            text = ""
            for src in flight:
                cc = "gcc" if tool in ("gcc", "fanalyzer") else "clang"
                cmd = [cc, *flags, *bflags, *inc, "-c", src, "-o", "/dev/null"]
                if tool == "analyzer":
                    cmd = [cc, *flags, *bflags, *inc, src, "-o", "/dev/null"]
                out = run(cmd)
                text += f"### {' '.join(cmd)}\n{out}\n"
            with open(os.path.join(raw, f"{tool}-{bname}.txt"), "w") as f:
                f.write(text.replace(ROOT + "/", ""))
            for d in parse(tool, text, flight):
                d["build"] = bname
                found.append(d)

        # cppcheck: whole program, with the MISRA addon
        cdir = os.path.join(raw, f"cppcheck-{bname}")
        os.makedirs(cdir)
        cmd = ["cppcheck", "--enable=all", "--inconclusive", "--std=c99", "--language=c",
               "--inline-suppr", "--addon=misra", "--suppress=missingIncludeSystem",
               "--suppress=unmatchedSuppression", f"--cppcheck-build-dir={cdir}",
               "--template={file}:{line}:{column}: {severity}: {message} [{id}]",
               *bflags, *[i for i in inc if i.startswith("-I")], *flight]
        text = run(cmd)
        with open(os.path.join(raw, f"cppcheck-{bname}.txt"), "w") as f:
            f.write(text.replace(ROOT + "/", ""))
        shutil.rmtree(cdir)
        for d in parse("cppcheck", text, flight):
            d["build"] = bname
            found.append(d)

    # Merge the two builds: one finding per key, remembering where it appears.
    merged = collections.OrderedDict()
    for d in found:
        key = f"{d['tool']}|{d['check']}|{d['file']}|{norm_msg(d['msg'])}"
        e = merged.setdefault(key, dict(d, key=key, lines=set(), builds=set()))
        e["lines"].add(d["line"])
        e["builds"].add(d["build"])

    disp_path = os.path.join(a.out, "dispositions.yaml")
    rules = yaml.safe_load(open(disp_path))["dispositions"] if os.path.exists(disp_path) else []

    def disposition(e):
        for r in rules:
            if r.get("tool", e["tool"]) != e["tool"]:
                continue
            if "check" in r and not re.fullmatch(r["check"], e["check"]):
                continue
            if "file" in r and not re.search(r["file"], e["file"]):
                continue
            if "msg" in r and not re.search(r["msg"], e["msg"]):
                continue
            return r
        return None

    rows, new, used = [], [], set()
    for e in merged.values():
        r = disposition(e)
        if r is not None:
            used.add(r["id"])
        rows.append((e, r))
        if r is None or r["disposition"] == "fixed":
            new.append(e)        # a "fixed" finding must not be present any more

    by_tool = collections.Counter(e["tool"] for e in merged.values())
    by_disp = collections.Counter((r or {}).get("disposition", "NEW") for _, r in rows)
    with open(os.path.join(a.out, "findings.json"), "w") as f:
        json.dump([dict({k: v for k, v in e.items() if k not in ("lines", "builds")},
                        lines=sorted(e["lines"]), builds=sorted(e["builds"]),
                        disposition=(r or {}).get("id")) for e, r in rows], f, indent=1)

    lines = ["# Static analysis: current findings (generated)", "",
             "Generated by `tests/support/static_analysis.py` from the raw tool outputs in "
             "`raw/`. Every finding is matched to a disposition in `dispositions.yaml`; the "
             "triage, with its reasoning and the findings already fixed, is in "
             "`findings-log.md`.", "",
             "Tools: " + "; ".join(f"{k}: `{v}`" for k, v in versions.items()), "",
             "| Tool | Findings (distinct, both builds) |", "|---|---:|"]
    for t in ("gcc", "clang", "cppcheck", "analyzer", "fanalyzer"):
        lines.append(f"| {t} | {by_tool.get(t, 0)} |")
    lines += ["", "| Disposition | Findings |", "|---|---:|"]
    for k, v in sorted(by_disp.items()):
        lines.append(f"| {k} | {v} |")
    lines += ["", "| Tool | Check | File | Lines | Message | Disposition |", "|---|---|---|---|---|---|"]
    for e, r in rows:
        ls = sorted(e["lines"])
        lstr = ", ".join(map(str, ls[:6])) + (f" (+{len(ls) - 6})" if len(ls) > 6 else "")
        msg = e["msg"].replace("|", "\\|")
        lines.append(f"| {e['tool']} | `{e['check']}` | `{e['file']}` | {lstr} | {msg} | "
                     f"{r['id'] + ' ' + r['disposition'] if r else '**NEW**'} |")
    with open(os.path.join(a.out, "findings-current.md"), "w") as f:
        f.write("\n".join(lines) + "\n")

    unused = [r["id"] for r in rules if r["id"] not in used and r["disposition"] != "fixed"]
    print(f"static analysis: {len(merged)} distinct findings "
          f"({', '.join(f'{t} {n}' for t, n in by_tool.items())}); "
          f"dispositions: {dict(by_disp)}")
    if unused:
        print(f"note: dispositions matching nothing now: {unused}")
    if new:
        print(f"{len(new)} finding(s) without a valid disposition:")
        for e in new[:40]:
            print(f"  {e['tool']} [{e['check']}] {e['file']}:{min(e['lines'])}: {e['msg']}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
