#!/usr/bin/env python3
"""Conformance runner (runs inside the dev container).

  run.py ref   [tests...]   render references on the emulated Voodoo with a
                            retail OVL -> tests/conform/ref/voodoo/<t>/*.png
  run.py check [tests...]   run with MGA-Glide on the G100, compare against
                            the references -> out/conform/<t>/, summary.json
"""
import concurrent.futures as cf
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GLIDE = os.path.dirname(os.path.dirname(HERE))   # glide/: the suite and its references
ROOT = os.path.dirname(GLIDE)   # the repository: build/, out/, tools/
sys.path.insert(0, os.path.join(ROOT, "tools"))
import imgcmp  # noqa: E402

CACHE = os.environ.get("MGA_CACHE", os.path.expanduser("~/.cache/mga-glide"))
FIX = os.environ.get("FIXTURES_DIR", os.path.join(CACHE, "fixtures"))
REF_OVL = os.environ.get("REF_OVL", os.path.join(FIX, "ovl", "gta.ovl"))
MGA_OVL = os.path.join(ROOT, "build", "ow", "GLIDE2X.OVL")
EXE = os.path.join(ROOT, "build", "ow", "dos", "CONFORM.EXE")
REFDIR = os.path.join(GLIDE, "tests", "conform", "ref", "voodoo")
CARD = os.environ.get("MGA_CARD", "g100")
CHIP = CARD[:-3] if CARD.endswith("pci") else CARD     # (a PCI card draws as its AGP twin: 86Box patch 0116)
# Manifest entries keyed by card: the G400 family draws with the G200's
# combine and blend capabilities, so it shares the G200's expectations.
CARD_CLASS = {"g400": "g200", "g450": "g200"}.get(CHIP, CHIP)
# The G200 and the G400 family set triangles up with the WARP (on the G200
# since G7: docs/g200-warp.md) unless MGA_OPTS turns it off with warp=0; a
# test's "warp" entry then holds (its own edge rule: docs/g400-warp.md).
WARP = CHIP in ("g200", "g400", "g450") and \
    "warp=0" not in os.environ.get("MGA_OPTS", "").replace(";", " ").split()
MANIFEST = json.load(open(os.path.join(GLIDE, "tests", "conform", "manifest.json")))
JOBS = int(os.environ.get("LOOPA_JOBS", "6"))


def tests_from(args):
    return args or sorted(k for k in MANIFEST if not k.startswith("_"))


def run_one(test, ovl, outdir):
    cmd = [sys.executable, os.path.join(ROOT, "tools", "loopa", "run.py"), "--name", test,
           "--exe", EXE, "--args", test, "--ovl", ovl, "--out", outdir,
           "--timeout", "180", "--idle", "60", "--card", CARD]
    if os.environ.get("MGA_OPTS"):             # runtime options for every test, e.g. "bpp=32"
        cmd += ["--pre", "SET MGAGLIDE=" + os.environ["MGA_OPTS"]]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return json.load(open(os.path.join(outdir, "result.json")))


def cmd_ref(tests):
    if not os.path.exists(REF_OVL):
        sys.exit("reference OVL %s missing (run tools/fixtures/extract.py)" % REF_OVL)
    import hashlib
    sha = hashlib.sha256(open(REF_OVL, "rb").read()).hexdigest()

    tests = [t for t in tests if not MANIFEST.get(t, {}).get("nocompare")]

    def job(t):
        out = os.path.join(ROOT, "out", "conform-ref", t)
        r = run_one(t, REF_OVL, out)
        dst = os.path.join(REFDIR, t)
        shutil.rmtree(dst, ignore_errors=True)
        os.makedirs(dst)
        for fn in sorted(os.listdir(out)):
            if fn.startswith(t + "_") and fn.endswith(".png") and ".diff" not in fn:
                shutil.copyfile(os.path.join(out, fn), os.path.join(dst, fn))
        json.dump({"ovl_sha256": sha, "status": r["status"], "tests": r.get("tests", [])},
                  open(os.path.join(dst, "ref.json"), "w"), indent=1)
        return t, r["status"], len([f for f in os.listdir(dst) if f.endswith(".png")])
    with cf.ThreadPoolExecutor(JOBS) as ex:
        for t, st, n in ex.map(job, tests):
            print("ref %-6s %-10s %d frames" % (t, st, n))


def cmd_check(tests):
    summary = {}

    def job(t):
        out = os.path.join(ROOT, "out", "conform", t)
        r = run_one(t, MGA_OVL, out)
        cfg = dict(MANIFEST["_default"])
        cfg.update(MANIFEST.get(t, {}))
        if WARP:
            cfg.update(cfg.get("warp", {}))     # the WARP's own edges (docs/g400-warp.md)
        frames = {}
        refd = os.path.join(REFDIR, t)
        ok = r["status"] == "PASS"
        if cfg.get("nocompare"):
            return t, {"run": r["status"], "ok": ok, "frames": {}, "stats": r.get("stats", []),
                       "tests": [x for x in r.get("tests", []) if x["result"] != "PASS"]}
        if os.path.isdir(refd):
            for fn in sorted(os.listdir(refd)):
                if not fn.endswith(".png"):
                    continue
                got = os.path.join(out, fn)
                if not os.path.exists(got):
                    frames[fn] = {"ok": False, "reason": "missing"}
                    ok = False
                    continue
                fc = dict(cfg)
                fc.update(cfg.get("frames", {}).get(fn, {}))
                cells = fc.get("cells")
                if cells and isinstance(cells.get("approx"), dict):
                    fc["cells"] = dict(cells, approx=cells["approx"].get(CHIP, cells["approx"].get(CARD_CLASS, [])))
                c = imgcmp.compare(os.path.join(refd, fn), got, fc["tol"], fc["frac"], fc["edge"],
                                   fc["box"], os.path.join(out, fn[:-4] + ".diff.png"), fc.get("ignore", ()),
                                   fc.get("cells"))
                c["gate"] = fc.get("gate", True)
                frames[fn] = c
                ok = ok and (c["ok"] or not c["gate"])
        else:
            ok = False
            frames["_"] = {"ok": False, "reason": "no reference"}
        return t, {"run": r["status"], "ok": ok, "frames": frames,
                   "tests": [x for x in r.get("tests", []) if x["result"] != "PASS"]}
    with cf.ThreadPoolExecutor(JOBS) as ex:
        for t, res in ex.map(job, tests):
            summary[t] = res
            worst = max([f.get("frac", 1.0) for f in res["frames"].values() if f.get("gate", True)] or [0])
            print("check %-6s %-4s run=%-9s frames=%d worst-frac=%.4f %s" % (
                t, "PASS" if res["ok"] else "FAIL", res["run"], len(res["frames"]), worst,
                "; ".join("%s %s" % (x["name"], x["detail"]) for x in res["tests"])))
    os.makedirs(os.path.join(ROOT, "out", "conform"), exist_ok=True)
    json.dump(summary, open(os.path.join(ROOT, "out", "conform", "summary.json"), "w"), indent=1)
    json.dump(summary, open(os.path.join(ROOT, "out", "conform", "summary-%s.json" % CARD), "w"), indent=1)
    return 0 if all(v["ok"] for v in summary.values()) else 1


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("ref", "check"):
        sys.exit(__doc__)
    tests = tests_from(sys.argv[2:])
    if sys.argv[1] == "ref":
        cmd_ref(tests)
        return 0
    return cmd_check(tests)


if __name__ == "__main__":
    sys.exit(main())
