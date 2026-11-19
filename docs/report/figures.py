"""Figures for the technical report, from committed evidence only.

    python3 docs/report/figures.py      -> docs/report/fig-*.png
"""
import os
import re

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
LOG = os.path.join(ROOT, "evidence", "demo", "scenario-host-sim.log")
INK, MUTED, GRID = "#222222", "#666666", "#dddddd"
MODE_COL = {"NOMINAL": "#d9ecd9", "DEGRADED": "#fbe7b5", "SAFE": "#f6c9c4"}


def timeline():
    hk, events, modes = [], [], []
    rx = re.compile(r"t=\s*([\d.]+)s TM\((\d+),(\d+)\)\s+(.*)$")
    for line in open(LOG, errors="replace"):
        m = rx.search(line)
        if not m:
            continue
        t, svc, text = float(m.group(1)), int(m.group(2)), m.group(4)
        if svc == 3 and text.startswith("HK"):
            tb = re.search(r"Tbat (-?[\d.]+)", text)
            md = re.search(r"HK\d (\w+)", text)
            if tb:
                hk.append((t, float(tb.group(1)), md.group(1)))
        elif svc == 5:
            events.append((t, text))
            mm = re.search(r"MODE (\w+) -> (\w+)", text)
            if mm:
                modes.append((t, mm.group(2)))
    # The log holds the first contact boot too: keep the last boot only.
    boots = [t for t, e in events if e.startswith("BOOT")]
    t0 = boots[-1] if boots else 0.0
    last_boot_idx = max(i for i, (t, e) in enumerate(events) if e.startswith("BOOT")) if boots else 0
    events = events[last_boot_idx:]
    hk = [h for h in hk if h[0] >= t0][: len(hk)]
    end = max(t for t, *_ in hk)

    fig, ax = plt.subplots(figsize=(9.5, 3.6), dpi=200)
    seg = [(t0, "NOMINAL")] + [m for m in modes if m[0] >= t0] + [(end, None)]
    for (a, md), (b, _) in zip(seg, seg[1:]):
        ax.axvspan(a, b, color=MODE_COL[md], lw=0)
        ax.text((a + b) / 2, 50.6, md, ha="center", va="bottom", fontsize=8, color=MUTED)
    ax.plot([h[0] for h in hk], [h[1] for h in hk], color=INK, lw=1.4, label="T_BAT (HK)")
    ax.axhline(45.0, color="#b33", lw=0.8, ls="--")
    ax.text(end, 45.15, "alarm 45.00 C", ha="right", va="bottom", fontsize=7, color="#b33")
    ax.axhline(43.0, color=MUTED, lw=0.8, ls=":")
    ax.text(end, 42.4, "clear 43.00 C", ha="right", va="top", fontsize=7, color=MUTED)
    marks = [("REJECT_FIRST", "forged TC\nrejected"), ("MONITOR T_BAT -> ALARM", "ALARM\n(3 samples)"),
             ("attempt 1/3", "att. 1"), ("attempt 2/3", "att. 2"), ("attempt 3/3", "att. 3"),
             ("EXHAUSTED", "exhausted"), ("SAFE -> NOMINAL", "ground\nSAFE exit")]
    for i, (key, label) in enumerate(marks):
        hit = next((t for t, e in events if key in e), None)
        if hit is None:
            continue
        ax.axvline(hit, color=MUTED, lw=0.6)
        ax.text(hit + 0.4, 20.5 if i % 2 == 0 else 26.0, label, fontsize=6.5, color=INK, va="bottom")
    ax.set_xlim(t0, end)
    ax.set_ylim(20, 52)
    ax.set_xlabel("onboard time (s)", fontsize=8, color=INK)
    ax.set_ylabel("T_BAT (C)", fontsize=8, color=INK)
    ax.tick_params(labelsize=7, colors=MUTED)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.grid(axis="y", color=GRID, lw=0.5)
    fig.tight_layout()
    out = os.path.join(HERE, "fig-scenario-timeline.png")
    fig.savefig(out)
    print("wrote", out)


if __name__ == "__main__":
    timeline()
