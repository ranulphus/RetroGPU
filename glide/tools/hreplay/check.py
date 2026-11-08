#!/usr/bin/env python3
"""hreplay check (PRD M3.6): record untextured conformance tests in Loop A
(MGAGLIDE trace=1), replay each trace on the host into the reference
rasteriser, and require every captured frame to be bit-identical to the
Loop A capture of the same run.

  check.py [tests...]      default: the untextured tests
"""
import concurrent.futures as cf
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))   # the repository (build/, out/, tools/)
sys.path.insert(0, os.path.join(ROOT, "tools", "loopa"))
import png  # noqa: E402

UNTEXTURED = ["t01", "t02", "t03", "t04", "t05", "t06", "t07", "t08", "t09", "t21"]


def one(t):
    out = os.path.join(ROOT, "out", "hreplay", t)
    rec = os.path.join(out, "loopa")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/loopa/run.py"), "--name", "hreplay-" + t,
                    "--exe", os.path.join(ROOT, "build/ow/dos/CONFORM.EXE"), "--args", t,
                    "--ovl", os.path.join(ROOT, "build/ow/GLIDE2X.OVL"), "--out", rec,
                    "--pre", "SET MGAGLIDE=trace=1 trace_path=C:\\OUT\\T.BIN", "--idle", "60", "--timeout", "180"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    trace = os.path.join(rec, "files", "T.BIN")
    if not os.path.exists(trace):
        return t, "NO-TRACE", []
    host = os.path.join(out, "host")
    os.makedirs(host, exist_ok=True)
    for f in os.listdir(host):
        os.remove(os.path.join(host, f))
    subprocess.run([os.path.join(ROOT, "build/host32/hreplay"), trace, host, "0"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    res, ok = [], True
    reads = sorted((f for f in os.listdir(host) if f.startswith("hrp_read")), key=lambda f: int(f[8:-4]))
    for i, fn in enumerate(reads):
        ref = os.path.join(rec, "%s_%d.png" % (t, i))
        if not os.path.exists(ref):
            res.append((fn, "no Loop A frame"))
            ok = False
            continue
        _w, _h, a = png.read_ppm(os.path.join(host, fn))
        _w, _h, b = png.read_png(ref)
        diff = sum(1 for k in range(0, len(a), 3) if a[k:k + 3] != b[k:k + 3])
        res.append((fn, diff))
        ok = ok and diff == 0
    if not reads:
        ok = False
    return t, "PASS" if ok else "FAIL", res


def main():
    tests = sys.argv[1:] or UNTEXTURED
    bad = 0
    with cf.ThreadPoolExecutor(int(os.environ.get("LOOPA_JOBS", "4"))) as ex:
        for t, st, res in ex.map(one, tests):
            print("hreplay %-4s %-8s %s" % (t, st, " ".join("%s:%s" % r for r in res)))
            bad += st != "PASS"
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
