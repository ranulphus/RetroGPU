#!/usr/bin/env python3
"""Loop C: DOS-GL (make rig) on a real Matrox chip in a Linux machine, over ssh.

Stages, each writing to the card only when run; stop at the first anomaly:
  info      read-only: identity, BARs, bound driver (no card access)
  smoke     MGA-Glide's mgarig: read registers, DWGSYNC, one fill, one triangle
  conform   DOS-GL's conformance tests; frames compared with Mesa's (tools/conform)
  bench     tests/rig/rigbench: chip time per triangle, fill rate, register write rate

  tools/rig/run.py --host retro@devserver --bdf 0000:09:00.0 STAGE... \
      [--unbind] [--vram-kb 8192] [--vram-base-kb 0] [--mgarig PATH]

--unbind detaches the host's driver (mgag200) for the session and binds it
again afterwards, whatever happens; without it DOS-GL draws only from
--vram-base-kb up (above the console) and never touches the display.
Outputs go to out/rig/<host>/. Machines doing other work: agree a window
with their owner first (docs/rig.md).
"""
import argparse
import json
import os
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REMOTE = "mga-rig/dosgl"


def ssh(host, cmd, check=True, capture=True):
    r = subprocess.run(["ssh", "-o", "BatchMode=yes", host, cmd], text=True,
                       stdout=subprocess.PIPE if capture else None, stderr=subprocess.STDOUT)
    if check and r.returncode:
        raise SystemExit("rig: %s: %s failed:\n%s" % (host, cmd, r.stdout))
    return r.stdout if capture else ""


def driver(host, bdf):
    out = ssh(host, "readlink /sys/bus/pci/devices/%s/driver || true" % bdf)
    return os.path.basename(out.strip()) if out.strip() else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stages", nargs="+", choices=["info", "smoke", "conform", "bench"])
    ap.add_argument("--host", required=True)
    ap.add_argument("--bdf", required=True)
    ap.add_argument("--unbind", action="store_true")
    ap.add_argument("--vram-kb", type=int, default=8192)
    ap.add_argument("--vram-base-kb", type=int, default=0)
    ap.add_argument("--mgarig", default=os.path.expanduser("~/MGA-Glide/build/mgarig"))
    ap.add_argument("--tests", default="", help="conformance tests (default all)")
    a = ap.parse_args()
    out = os.path.join(ROOT, "out", "rig", a.host.split("@")[-1])
    os.makedirs(out, exist_ok=True)
    env = "RIG_BDF=%s DGL_RIG_VRAM_KB=%d DGL_RIG_VRAM_BASE=%d" % (a.bdf, a.vram_kb, a.vram_base_kb)
    summary = {"host": a.host, "bdf": a.bdf, "stages": {}}

    was = driver(a.host, a.bdf)
    print("rig: %s %s, driver %s" % (a.host, a.bdf, was or "none"))
    if "info" in a.stages:
        info = ssh(a.host, "lspci -nn -s %s; cat /sys/bus/pci/devices/%s/resource | head -3" % (a.bdf[5:], a.bdf))
        open(os.path.join(out, "info.txt"), "w").write(info)
        print(info.rstrip())
    writes = [s for s in a.stages if s != "info"]
    if not writes:
        return 0
    if was and not a.unbind and a.vram_base_kb == 0:
        raise SystemExit("rig: %s is bound to %s: pass --unbind, or --vram-base-kb above its console" % (a.bdf, was))

    ssh(a.host, "mkdir -p %s/conform %s/out" % (REMOTE, REMOTE))
    files = [os.path.join(ROOT, "build/rig/rigbench")]
    tests = a.tests.split(",") if a.tests else sorted(os.listdir(os.path.join(ROOT, "build/rig/conform")))
    files += [os.path.join(ROOT, "build/rig/conform", t) for t in tests]
    subprocess.run(["scp", "-q"] + [f for f in files if os.path.exists(f)] + ["%s:%s/" % (a.host, REMOTE)], check=True)
    if "smoke" in a.stages:
        subprocess.run(["scp", "-q", a.mgarig, "%s:%s/" % (a.host, REMOTE)], check=True)

    unbound = False
    try:
        if a.unbind and was:
            ssh(a.host, "echo %s | sudo tee /sys/bus/pci/drivers/%s/unbind >/dev/null" % (a.bdf, was))
            unbound = True
            print("rig: %s unbound from %s" % (a.bdf, was))
        for stage in writes:
            if stage == "smoke":
                for step in ("regs", "sync", "trap", "tex"):
                    r = ssh(a.host, "cd %s && sudo %s ./mgarig %s" % (REMOTE, env, step), check=False)
                    open(os.path.join(out, "smoke-%s.txt" % step), "w").write(r)
                    ok = "FAIL" not in r and "error" not in r.lower()
                    print("rig: smoke %s: %s" % (step, "ok" if ok else "ANOMALY"))
                    summary["stages"]["smoke-" + step] = ok
                    if not ok:
                        raise SystemExit("rig: stopping at smoke %s (out/rig/.../smoke-%s.txt)" % (step, step))
            elif stage == "conform":
                for t in tests:
                    r = ssh(a.host, "cd %s && sudo CT_OUT=out %s ./%s" % (REMOTE, env, t), check=False)
                    open(os.path.join(out, "conform-%s.txt" % t), "w").write(r)
                    done = "HX-DONE 0" in r and " FAIL" not in r
                    print("rig: conform %s: %s" % (t, "ran" if done else "ANOMALY"))
                    summary["stages"]["conform-" + t] = done
                    if not done:
                        raise SystemExit("rig: stopping at %s" % t)
                subprocess.run(["scp", "-q", "%s:%s/out/*.ppm" % (a.host, REMOTE), out], check=False)
            elif stage == "bench":
                r = ssh(a.host, "cd %s && sudo %s ./rigbench 7" % (REMOTE, env), check=False)
                open(os.path.join(out, "bench.txt"), "w").write(r)
                print(r.rstrip())
                summary["stages"]["bench"] = "HX-DONE 0" in r
    finally:
        if unbound:
            ssh(a.host, "echo %s | sudo tee /sys/bus/pci/drivers/%s/bind >/dev/null" % (a.bdf, was), check=False)
            print("rig: %s bound to %s again (now: %s)" % (a.bdf, was, driver(a.host, a.bdf)))
        json.dump(summary, open(os.path.join(out, "summary.json"), "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
