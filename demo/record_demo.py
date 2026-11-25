#!/usr/bin/env python3
"""Record the LOTUS-1 demonstration through Yamcs as a video, unattended.

Needs Yamcs running (mcs/run-yamcs.sh), Python Playwright with Chromium, and
ffmpeg. This script restarts the spacecraft behind the bridge (fresh key
store) and opens three Yamcs views in a headless browser: operator.par,
Events and Command history. It then runs demo/run_demo.py and records the
three views, and finally composes them into one 1920x1080 video with the
captions burned in and an end card (CI status and coverage).

    python3 demo/record_demo.py [--out evidence/demo/lotus1-demo.mp4] [--pace 1.8]

Output: the video, evidence/demo/captions.srt, evidence/demo/yamcs-demo-run.txt.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
P = argparse.ArgumentParser(description=__doc__.split("\n")[0])
P.add_argument("--out", default=os.path.join(ROOT, "evidence", "demo", "lotus1-demo.mp4"))
P.add_argument("--pace", type=float, default=1.8)
P.add_argument("--url", default="http://localhost:8090")
P.add_argument("--sis", action="store_true", help="spacecraft on SIS (RTEMS_PREFIX) instead of the host")
A = P.parse_args()
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
W = tempfile.mkdtemp(prefix="lotus1-rec-")


def links_ok():
    try:
        with OPENER.open(A.url + "/api/links/lotus1", timeout=3) as r:
            return all(l["status"] == "OK" for l in json.load(r)["links"])
    except OSError:
        return False


def yamcs_up():
    try:
        with OPENER.open(A.url + "/api/links/lotus1", timeout=3):
            return True
    except OSError:
        return False


def wait(cond, timeout, what):
    t = time.time() + timeout
    while time.time() < t:
        if cond():
            return
        time.sleep(1)
    sys.exit(f"timeout waiting for {what}")


def main():
    for tool in ("ffmpeg",):
        if shutil.which(tool) is None:
            sys.exit(f"{tool} not found (sudo apt install ffmpeg)")
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        sys.exit("Python Playwright not found: pip install --user playwright && "
                 "python3 -m playwright install chromium")
    wait(yamcs_up, 10, "Yamcs at " + A.url + " (start mcs/run-yamcs.sh first)")

    views = [("disp", "/telemetry/displays/files/operator.par", (1200, 1250)),
             ("ev", "/events", (1200, 625)),
             ("hist", "/commanding/history", (1200, 625))]
    bridge = None
    with sync_playwright() as p:
        b = p.chromium.launch()
        tstart = time.time()
        pages = {}
        for k, path, (w, h) in views:
            ctx = b.new_context(viewport={"width": w, "height": h},
                                record_video_dir=os.path.join(W, k),
                                record_video_size={"width": w, "height": h})
            pg = ctx.new_page()
            pg.goto(A.url + path + "?c=lotus1__realtime")
            pages[k] = (ctx, pg)
        time.sleep(3)
        for k, (ctx, pg) in pages.items():
            for label in ("Collapse sidebar",) + (("Start streaming",) if k != "disp" else ()):
                try:
                    pg.get_by_text(label).click(timeout=3000)
                except Exception:                                   # noqa: BLE001
                    pass
            pg.mouse.move(pg.viewport_size["width"] - 10, pg.viewport_size["height"] - 10)

        # The spacecraft restarts cold on camera.
        subprocess.run(["pkill", "-f", "bridge/" + "sc_bridge.py"], check=False)
        time.sleep(1)
        cmd = ["sh", os.path.join(ROOT, "mcs", "run-bridge.sh"), "fresh"] + (["sis"] if A.sis else [])
        bridge = subprocess.Popen(cmd, stdout=open(os.path.join(W, "bridge.out"), "w"),
                                  stderr=subprocess.STDOUT)
        time.sleep(3)
        wait(links_ok, 120, "the Yamcs links after the spacecraft restart")
        time.sleep(4)
        demo_t0 = time.time()
        srt = os.path.join(ROOT, "evidence", "demo", "captions-raw.srt")
        log = os.path.join(ROOT, "evidence", "demo", "yamcs-demo-run.txt")
        rc = subprocess.run([sys.executable, os.path.join(HERE, "run_demo.py"), "--pace", str(A.pace),
                             "--srt", srt], stdout=open(log, "w"), stderr=subprocess.STDOUT).returncode
        time.sleep(5)
        t1 = time.time()
        vids = {}
        for k, (ctx, pg) in pages.items():
            v = pg.video
            ctx.close()
            vids[k] = v.path()
        b.close()
    print(open(log).read().splitlines()[-2] if rc == 0 else open(log).read()[-2000:])

    # Captions: one at a time, each until the next; times relative to the cut.
    trim = 12.0
    caps = []
    for blk in open(srt).read().strip().split("\n\n"):
        lines = blk.split("\n")
        h, m, s = lines[1].split(" --> ")[0].replace(",", ".").split(":")
        caps.append([int(h) * 3600 + int(m) * 60 + float(s) + demo_t0 - tstart, " ".join(lines[2:])])
    total = t1 - tstart
    commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True,
                            text=True).stdout.strip()
    items = [[trim, "LOTUS-1 operated from Yamcs. The spacecraft has just restarted cold; first contact "
                    "sets the key-store counter floor"]] + caps
    ends = [c[0] for c in items[1:]] + [total + 10]

    def ts(x):
        ms = int(round(max(0.0, x - trim) * 1000))
        return f"{ms // 3600000:02d}:{ms // 60000 % 60:02d}:{ms // 1000 % 60:02d},{ms % 1000:03d}"
    with open(os.path.join(ROOT, "evidence", "demo", "captions.srt"), "w") as f:
        for i, ((st, txt), en) in enumerate(zip(items, ends), 1):
            if txt.startswith("7/7"):
                txt = f"7/7  Same commit {commit}: CI and coverage"
            f.write(f"{i}\n{ts(st)} --> {ts(en)}\n{txt}\n\n")

    # End card from the committed evidence (and the CI status, if gh is set up).
    ci = "see the GitHub Actions page of this commit"
    if shutil.which("gh"):
        r = subprocess.run(["gh", "run", "list", "--workflow", "fast-checks.yml", "--commit",
                            subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True,
                                           text=True).stdout.strip(),
                            "--json", "conclusion,databaseId", "-q", ".[0]"], cwd=ROOT,
                           capture_output=True, text=True)
        try:
            j = json.loads(r.stdout)
            ci = f"fast-checks run {j['databaseId']}: {j['conclusion'] or 'in progress'}"
        except (ValueError, KeyError, TypeError):
            pass
    cov = next((l for l in open(os.path.join(ROOT, "evidence", "coverage", "coverage.md"))
                if "All flight code" in l), "")
    covp = cov.split("|")[7].strip(" *") if cov else "?"
    card = os.path.join(W, "card.html")
    open(card, "w").write(f"""<!doctype html><meta charset=utf-8><body style="margin:0;width:1920px;
height:1000px;background:#F7F6F2;color:#14213D;font-family:Inter,Arial,sans-serif"><div style="padding:110px">
<p style="font-size:30px;color:#C8611C;margin:0">7/7</p><h1 style="font-size:80px;margin:10px 0 40px">Same commit
{commit}: CI and coverage</h1><p style="font-size:40px;margin:0 0 24px">CI: {ci}</p><p style="font-size:40px;
margin:0 0 24px">Coverage: {covp} of flight lines, cumulative unit + integration + validation
(evidence/coverage/coverage.md)</p><p style="font-size:32px;color:#5B6575">Demo checks: see
evidence/demo/yamcs-demo-run.txt</p></div>""")
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page(viewport={"width": 1920, "height": 1000})
        pg.goto("file://" + card)
        pg.screenshot(path=os.path.join(W, "card.png"))
        b.close()

    d = round(total - trim, 2)
    fc = ("[0:v]scale=960:1000,setsar=1[a];[1:v]scale=960:500,setsar=1[b];[2:v]scale=960:500,setsar=1[c];"
          "[b][c]vstack[r];[a][r]hstack,fps=25[m];[3:v]scale=1920:1000,fps=25,setsar=1[e];"
          "[m][e]concat=n=2:v=1[mm];[mm]pad=1920:1080:0:0:color=0x14213D,subtitles="
          + os.path.join(ROOT, "evidence", "demo", "captions.srt").replace(":", "\\:")
          + ":force_style='FontName=Inter,FontSize=8,Bold=1,PrimaryColour=&H00EAF0F2,Alignment=2,"
            "MarginV=6,BorderStyle=1,Outline=0,Shadow=0'[out]")
    cmd = ["ffmpeg", "-v", "error", "-y"]
    for k in ("disp", "ev", "hist"):
        cmd += ["-ss", str(trim), "-t", str(d), "-i", vids[k]]
    cmd += ["-loop", "1", "-t", "10", "-i", os.path.join(W, "card.png"), "-filter_complex", fc,
            "-map", "[out]", "-c:v", "libx264", "-preset", "veryfast", "-crf", "23", "-pix_fmt", "yuv420p",
            A.out]
    subprocess.run(cmd, check=True)
    os.remove(srt)
    print(f"video: {A.out} ({d + 10:.0f} s)")
    if bridge:
        bridge.terminate()
    return rc


if __name__ == "__main__":
    sys.exit(main())
