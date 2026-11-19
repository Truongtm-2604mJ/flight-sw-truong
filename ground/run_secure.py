"""Run an unchanged Session 5 ground script against the Session 8 build:
every TC the script sends goes through the ground security unit.

    python3 run_secure.py <script.py> [script arguments...]
"""
import os
import runpy
import sys

import seclink

if len(sys.argv) < 2:
    raise SystemExit(__doc__)
script = os.path.abspath(sys.argv[1])
sys.argv = [script] + sys.argv[2:]
sys.path.insert(0, os.path.dirname(script))
seclink.install()
runpy.run_path(script, run_name="__main__")
