# Lab 6.4 - Traceability

- `requirements.yaml`: 15 mode management requirements, MM-010 to MM-150, each with its source.
- `build_matrix.py`: builds `traceability.md` from the evidence files. Nothing in the matrix is typed by hand:
  - model elements come from the `req:` tags in `mode_machine.yaml`;
  - generated artefacts and line numbers come from the generator's `mode_sm_trace.json`;
  - results come from the Lab 6.3 and generator test outputs.

  A requirement with no model element, no generated artefact or no passing test is reported as a GAP, and the script then exits non zero.
- `traceability.md`: the matrix.

```
make -C ../lab-6.3-equivalence test     # produces the results the matrix reads
python3 build_matrix.py
```

## Deriving requirements from an existing implementation

These requirements were written after the code they trace to, which is backwards from real practice.

**The main difficulty is telling intent from accident.** Reading `modes.c`, every behaviour looks equally deliberate. The order of two `if` statements decides whether a non-critical fault in DEGRADED under inhibit is "refused" or "no change" (ambiguity A4). Nothing in the code says whether that was designed or merely happened, and a requirement written from the code turns either into a rule.

**What the code never considered produces no requirement at all.** Simultaneous requests from different tasks (A1) only appeared once the model forced a priority to be written down. MM-110 and MM-130 exist because of Lab 6.1 and Lab 6.3, not because of the implementation.

**The trace looks complete by construction.** If the requirement is phrased as what the code does, every requirement is satisfied, so the matrix confirms the code instead of checking it. The tests derived alongside share the same blind spots. The defence used here is weak but real:

- write the requirements from the Session 5 design note rather than from the C;
- keep the rationale (`source`);
- add requirements where the formal model and the back-to-back harness showed behaviour the code did not define.

On a real project that order is reversed: requirements first, with their verification method, and code that has to be shown to meet them.
