#!/usr/bin/env python3
"""One-off porting tool, kept for review: converts a Session 4/5 host test
suite written with test_util.h (CHECK, CHECK_EQ, CHECK_MEM, RUN) into a
Unity suite. The ported files in tests/unit/ were produced by this script
and then edited by hand where noted in their header.

    python3 port_to_unity.py <old suite.c> <new suite.c> "<title>" [--drop fn ...]

Mapping:
    CHECK(c)            -> TEST_ASSERT_TRUE(c)
    CHECK_EQ(a, e)      -> TEST_ASSERT_EQUAL_INT64((int64_t)(e), (int64_t)(a))
    CHECK_MEM(a, e, n)  -> TEST_ASSERT_EQUAL_MEMORY((e), (a), (n))
    RUN(fn)             -> RUN_TEST(fn)
Unity stops a test case at its first failed assertion; test_util.h went on.
"""
import argparse
import re


def split_args(s, start):
    """s[start] is just after '('. Returns (args, index after ')')."""
    depth, args, cur, i = 0, [], [], start
    while True:
        ch = s[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            if depth == 0:
                args.append("".join(cur).strip())
                return args, i + 1
            depth -= 1
        elif ch == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        elif ch in "\"'":
            q = ch
            cur.append(ch)
            i += 1
            while s[i] != q:
                if s[i] == "\\":
                    cur.append(s[i])
                    i += 1
                cur.append(s[i])
                i += 1
        cur.append(s[i])
        i += 1


def convert_macro(s, name, fmt):
    out, i = [], 0
    pat = re.compile(r"\b" + name + r"\s*\(")
    while True:
        m = pat.search(s, i)
        if not m:
            out.append(s[i:])
            return "".join(out)
        out.append(s[i:m.start()])
        args, j = split_args(s, m.end())
        out.append(fmt(args))
        i = j


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("title")
    ap.add_argument("--drop", nargs="*", default=[])
    a = ap.parse_args()
    s = open(a.src).read()

    s = re.sub(r'#include "[./]*(?:lab-[^"]*/)?test_util\.h"\n', '#include "unity.h"\n\n#include <stdio.h>\n#include <string.h>\n', s)
    s = re.sub(r'#include "(?:\.\./)+(?:lab-[^/"]+/)?([^"/]+\.h)"', r'#include "\1"', s)
    s = convert_macro(s, "CHECK_MEM", lambda g: f"TEST_ASSERT_EQUAL_MEMORY(({g[1]}), ({g[0]}), ({g[2]}))")
    s = convert_macro(s, "CHECK_EQ", lambda g: f"TEST_ASSERT_EQUAL_INT64((int64_t)({g[1]}), (int64_t)({g[0]}))")
    s = convert_macro(s, "CHECK", lambda g: f"TEST_ASSERT_TRUE({g[0]})")
    s = convert_macro(s, "RUN", lambda g: f"RUN_TEST({g[0]})")
    s = s.replace("return TEST_SUMMARY();", "return UNITY_END();")
    s = re.sub(r'(int main\(void\)\n\{\n)(\s*)printf\("[^"]*"\);\n',
               lambda m: m.group(1) + m.group(2) + "UNITY_BEGIN();\n", s)
    for fn in a.drop:
        s = re.sub(r"\n\s*RUN_TEST\(" + fn + r"\);", "", s)
        s = re.sub(r"\nstatic void " + fn + r"\(void\)\n\{.*?\n\}\n", "\n", s, flags=re.S)
    header = (f"/* {a.title}\n *\n * Ported to Unity from {a.src.split('labs/')[-1]} by "
              "tests/support/port_to_unity.py.\n */\n")
    s = header + s
    if "void setUp(void)" not in s:
        s = s.replace("int main(void)", "void setUp(void) {}\nvoid tearDown(void) {}\n\nint main(void)", 1)
    open(a.dst, "w").write(s)


if __name__ == "__main__":
    main()
