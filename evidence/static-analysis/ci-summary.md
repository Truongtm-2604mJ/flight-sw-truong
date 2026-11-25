# Static analysis in CI (all five tools)

The committed `findings-current.md` was produced where cppcheck could not be installed. The full run with
all five tools happens in the fast-checks job on every push. Its summary line, for commit 5fcd74f
(GitHub Actions run 44120294053, job "Static analysis"):

    static analysis: 180 distinct findings (gcc 14, clang 28, analyzer 14, fanalyzer 1, cppcheck 123);
    dispositions: {'justified': 69, 'accepted': 99, 'open': 12}

- The 12 *open* findings were cppcheck/MISRA findings in files cppcheck had never seen before the capstone
  (SA-50, OBS-03). They have since been triaged into SA-51 to SA-57 and SA-50 is removed (OBS-03 closed);
  a run on the commit that closed it reports no open finding.
- The run before it, on commit 45eb083, failed with 5 findings that had no disposition. That run found
  NCR-05 (MISRA 5.9) and one `variableScope`. Both were fixed in 5fcd74f.
