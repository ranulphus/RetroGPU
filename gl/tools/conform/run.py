#!/usr/bin/env python3
"""Conformance suite (PRD §11.5, D16): each test's DOS build runs in 86Box
(Loop A) on the chosen card, its host build renders the reference with Mesa
OSMesa, and the frames are compared (RGB565 quantisation, edge masks,
per-test tolerances from tests/conform/manifest.json).

Runs inside the dev container (make conform CARD=g450 [TESTS="t01 t02"]
[PRE="SET DGL_GUARD_PX=800"], the last a RUN.BAT line before every test).
Results: out/conform/<card>/<test>/ (serial.log, frames, diffs, result.json)
and out/conform/summary-<card>.json. Exit status 0 when every test passes.

On a bench PC (make conform-bench PC=bench-g400): --refs makes the Mesa
references in the dev container, then --bench PC runs each test on the PC
through MGA-Glide's tools/bench/run.py (the checkout in MGA_GLIDE, default
~/MGA-Glide) on the host, one job per test, and compares as above. Results:
out/conform/<pc>/<test>/ and out/conform/summary-<pc>.json.
"""
import argparse
import concurrent.futures as cf
import glob
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MGAHAL = os.environ.get("MGA_GLIDE", os.path.expanduser("~/MGA-Glide"))     # its harness and image tools
sys.path.insert(0, os.path.join(MGAHAL, "tools"))
sys.path.insert(0, os.path.join(MGAHAL, "tools", "loopa"))
import imgcmp  # noqa: E402
import png  # noqa: E402

MANIFEST = json.load(open(os.path.join(ROOT, "tests", "conform", "manifest.json")))


def tests_available():
    return sorted(os.path.basename(p).split("_")[0] for p in glob.glob(os.path.join(ROOT, "tests/conform/t[0-9]*_*.c")))


PRE = []                                            # --pre: RUN.BAT lines for every test
MGA = MGAHAL


def run_dos(test, card, out):
    exe = os.path.join(ROOT, "build", "exe", "conform", test.upper() + ".EXE")
    cmd = [sys.executable, os.path.join(MGAHAL, "tools", "loopa", "run.py"), "--name", "%s-%s" % (card, test),
           "--exe", exe, "--card", card, "--out", out, "--timeout", "240", "--idle", "90"]
    for line in PRE + MANIFEST.get(test, {}).get("pre", []):   # RUN.BAT lines (e.g. SET DGL_...)
        cmd += ["--pre", line]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return json.load(open(os.path.join(out, "result.json")))


def run_bench(test, pc, out):
    """The test as one job on bench PC `pc`; its directory's frames and logs copied to `out`."""
    exe = os.path.join(ROOT, "build", "exe", "conform", test.upper() + ".EXE")
    cmd = [sys.executable, os.path.join(MGA, "tools", "bench", "run.py"), "--pc", pc, "--name", "conform-" + test,
           "--file", exe, "--timeout", "240", "--idle", "90"]
    for line in PRE + MANIFEST.get(test, {}).get("pre", []) + [os.path.basename(exe)]:
        cmd += ["--cmd", line]
    log = subprocess.run(cmd, cwd=MGA, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True).stdout
    jobs = sorted(glob.glob(os.path.join(MGA, "out", "bench", pc, "conform-%s-*" % test)), key=os.path.getmtime)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    open(os.path.join(out, "bench.log"), "w").write(log)
    if not jobs or not os.path.exists(os.path.join(jobs[-1], "result.json")):
        return {"status": "NO-JOB"}
    for f in os.listdir(jobs[-1]):
        if f.endswith((".png", ".log", ".json", ".txt")):
            shutil.copyfile(os.path.join(jobs[-1], f), os.path.join(out, f))
    return json.load(open(os.path.join(out, "result.json")))


def run_ref(test, out):
    os.makedirs(out, exist_ok=True)
    exe = os.path.join(ROOT, "build", "host", "conform", test)
    subprocess.run([exe], env=dict(os.environ, CT_OUT=out), check=True, stdout=subprocess.DEVNULL)
    for p in glob.glob(os.path.join(out, "*.ppm")):
        w, h, rgb = png.read_ppm(p)
        png.write_png(p[:-4] + ".png", w, h, rgb)


def check(test, card, pc=None):
    out = os.path.join(ROOT, "out", "conform", pc or card, test)
    ref = os.path.join(ROOT, "out", "conform", "ref", test)
    if pc:
        if not glob.glob(os.path.join(ref, "*.png")):
            sys.exit("conform: no references in %s (make them first: --refs, in the dev container)" % ref)
        res = run_bench(test, pc, out)
    else:
        run_ref(test, ref)
        res = run_dos(test, card, out)
    report = {"test": test, "card": res.get("card", card) if pc else card, "run": res["status"], "frames": {}}
    ok = res["status"] == "PASS"
    for rp in sorted(glob.glob(os.path.join(ref, "*.png"))):
        name = os.path.basename(rp)[:-4]
        got = os.path.join(out, name + ".png")
        m = dict(MANIFEST.get("_default", {}), **MANIFEST.get(name, {}))
        if not os.path.exists(got):
            report["frames"][name] = {"ok": False, "reason": "missing"}
            ok = False
            continue
        c = imgcmp.compare(rp, got, m.get("tol", 24), m.get("frac", 0.005), m.get("edge", True),
                           False, os.path.join(out, name + ".diff.png"), m.get("ignore", ()))
        report["frames"][name] = c
        ok = ok and c["ok"]
    report["ok"] = ok
    json.dump(report, open(os.path.join(out, "check.json"), "w"), indent=1)
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tests", nargs="*")
    ap.add_argument("--card", default=os.environ.get("CARD", "g450"))
    ap.add_argument("--jobs", type=int, default=int(os.environ.get("LOOPA_JOBS", "4")))
    ap.add_argument("--pre", action="append", default=[], help="RUN.BAT line before every test (SET DGL_...)")
    ap.add_argument("--bench", metavar="PC", help="run on this bench PC (on the host; references from --refs)")
    ap.add_argument("--refs", action="store_true", help="only make the Mesa references (in the dev container)")
    a = ap.parse_args()
    PRE.extend(a.pre)
    tests = a.tests or tests_available()
    if a.refs:
        for t in tests:
            run_ref(t, os.path.join(ROOT, "out", "conform", "ref", t))
        return 0
    with cf.ThreadPoolExecutor(1 if a.bench else a.jobs) as ex:   # one PC: one job at a time
        reports = list(ex.map(lambda t: check(t, a.card, a.bench), tests))
    for r in reports:
        worst = max([f.get("frac", 1.0) or 0 for f in r["frames"].values()] or [1.0])
        print("conform %-4s %-5s %s run=%-9s frames=%d worst-frac=%.4f" % (
            r["test"], r["card"], "PASS" if r["ok"] else "FAIL", r["run"], len(r["frames"]), worst), flush=True)
    json.dump(reports, open(os.path.join(ROOT, "out", "conform", "summary-%s.json" % (a.bench or a.card)), "w"), indent=1)
    return 0 if all(r["ok"] for r in reports) else 1


if __name__ == "__main__":
    sys.exit(main())
