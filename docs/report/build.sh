#!/bin/sh
# Build the technical report: report.md -> report.html -> LOTUS-1-technical-report.pdf
#   sh docs/report/build.sh        (pandoc, Python Playwright with Chromium)
set -eu
cd "$(dirname "$0")"
python3 figures.py
python3 fill.py > /dev/null
pandoc report.filled.md -s --toc --toc-depth=1 --mathml --css report.css --embed-resources \
    --metadata lang=en -o report.html
python3 - <<'PY'
import os
from playwright.sync_api import sync_playwright
here = os.getcwd()
with sync_playwright() as p:
    b = p.chromium.launch()
    pg = b.new_page()
    pg.goto("file://" + os.path.join(here, "report.html"))
    pg.pdf(path="LOTUS-1-technical-report.pdf", format="A4", print_background=True,
           display_header_footer=True, header_template="<span></span>",
           footer_template='<div style="font-size:8pt;width:100%;text-align:center;color:#666">'
                           'LOTUS-1 flight software, technical report - <span class="pageNumber"></span></div>',
           margin={"top": "18mm", "bottom": "18mm", "left": "17mm", "right": "17mm"})
    b.close()
PY
echo "wrote docs/report/LOTUS-1-technical-report.pdf"
