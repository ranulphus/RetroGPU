#!/usr/bin/env python3
"""Loop A self-tests for the harness's machine options (make loopa-selftest-ext).

Each check runs one short job through run.py and looks at its result:

  486     HELLO (DOS/4GW) and STACKPG (DJGPP) on the 486dx2 and 486dx4 profiles
  vbe     VBEINFO on the S3 Trio64V2/DX: VBE 2.0 with linear 640x480x16
  net     --net ne2k: mTCP listens on the guest's port 22; the host connects
          through SLiRP's port forwarding and its text arrives
  com2    --com2: COM2ECHO reads a line sent to the bridge's TCP port and
          echoes it back
  himemx  --boot-cfg himemx: XMS 3.0 with DOS in the HMA (XMSINFO), then
          HELLO (DOS/4GW) and STACKPG (DJGPP) on top of it
  cpu     V86TEST on all three profiles: every case passes or is a known
          86Box deviation (tests/cpu/known-86box.txt), and CR4 has the bits
          the profile's CPU has
  pci     the HAL's PROBE on the 486dx2 with each PCI Matrox card (local
          patch 0116): the card found, mapped, put in a VBE mode, and the
          engine filling
  rtc     RTCSET on bf6 and the 486dx2: a change of the RTC's century
          through the BIOS leaves registers A and B as they were and the
          year as set (the bf6's BIOS traps port 71h; local patch 0118)
  hazards 86Box's silicon checks on the G400 (local patches 0124, 0125):
          WARPTRI's own runs report none; WARPTRI hazards reports each
          kind as many times as it commits it; WARPTRI by AGP from cached
          conventional memory reports agp-cached, at 2x as on bench-g400;
          PROBE sees revision 05
  umbpci  --boot-cfg umbpci on the bf6 (a 440BX): UMBINFO finds free upper
          memory blocks (and none on the default boot), and HELLO runs
  clock   CLOCKMON: a minute of uclock() alone, then a minute of the DJGPP
          port's sys_time_us() alone: sys_time_us() never goes back, though
          uclock() steps back a tick (54.9 ms) 3 to 5 times a minute, when
          the PIT has wrapped and IRQ 0 hasn't yet counted it (the HAL's
          waits took that for a timeout)

Runs inside the dev container (tools/dev). Arguments pick checks; the
default is all of them. Exit status 1 if any check fails.
"""
import json
import re
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "out")


def job(name, *args):
    """Run one Loop A job; returns (status, serial text)."""
    subprocess.run([sys.executable, os.path.join(HERE, "run.py"), "--name", name, "--idle", "40",
                    "--boot-grace", "60", "--timeout", "240"] + list(args),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=ROOT)
    d = os.path.join(OUT, name)
    status = open(os.path.join(d, "status")).read().strip() if os.path.exists(os.path.join(d, "status")) else "?"
    serial = open(os.path.join(d, "serial.log"), "rb").read().decode("latin-1").replace("\r", "") \
        if os.path.exists(os.path.join(d, "serial.log")) else ""
    return status, serial


def report(name, ok, detail):
    print("  selftest %s: %s%s" % (name, detail, " (ok)" if ok else " (FAILED)"), flush=True)
    return ok


def check_486():
    ok = True
    for m in ("486dx2", "486dx4"):
        st, _ = job("selftest-%s-hello" % m, "--machine", m, "--exe", "build/ow/dos/HELLO.EXE")
        ok &= report("%s-hello" % m, st == "PASS", "HELLO (DOS/4GW) %s" % st)
        st, _ = job("selftest-%s-stackpg" % m, "--machine", m, "--exe", "build/djgpp/STACKPG.EXE")
        ok &= report("%s-stackpg" % m, st == "PASS", "STACKPG (DJGPP, CWSDPMI) %s" % st)
    return ok


def check_vbe():
    ok = True
    for m in ("bf6", "486dx2"):
        st, serial = job("selftest-vbe-%s" % m, "--machine", m, "--card", "vbe", "--voodoo", "0",
                         "--file", "build/ow/dos/VBEINFO.COM=/HX/VBEINFO.COM",
                         "--cmd", "SERSAY HX-START vbe", "--cmd", "VBEINFO", "--cmd", "SERSAY HX-DONE 0")
        line = next((l for l in serial.split("\n") if l.startswith("HX-VBE ")), "no HX-VBE line")
        good = st == "PASS" and "ver=0200" in line and "m640x480x16=none" not in line and " lfb=0" not in line
        ok &= report("vbe-%s" % m, good, "%s: %s" % (st, line))
    return ok


def check_net():
    st, serial = job("selftest-net", "--net", "ne2k", "--net-dos", "--cmd", "NE2000 0x60 10 0x300",
                     "--cmd", "DHCP", "--cmd", "SERSAY HX-START net", "--cmd", "SERSAY HX-NET listening",
                     "--cmd", "NC -LISTEN 22 > C:\\OUT\\NETRX.TXT", "--cmd", "SERSAY HX-DONE 0",
                     "--tcp-send", "HX-NET listening|net:22|GLOS-NET-PROBE")
    rx = os.path.join(OUT, "selftest-net", "files", "NETRX.TXT")
    got = open(rx, "rb").read().decode("latin-1") if os.path.exists(rx) else ""
    return report("net", st == "PASS" and "GLOS-NET-PROBE" in got,
                  "%s, guest received %r" % (st, got.strip()[:40]))


def check_com2():
    st, serial = job("selftest-com2", "--com2", "--file", "build/ow/dos/COM2ECHO.COM=/HX/COM2ECHO.COM",
                     "--cmd", "SERSAY HX-START com2", "--cmd", "COM2ECHO", "--cmd", "SERSAY HX-DONE 0",
                     "--tcp-send", "HX-COM2 ready|com2|GLOS-COM2-PROBE")
    back = os.path.join(OUT, "selftest-com2", "tcp-0.txt")
    echo = open(back, "rb").read().decode("latin-1") if os.path.exists(back) else ""
    return report("com2", st == "PASS" and "HX-COM2 got=GLOS-COM2-PROBE" in serial and
                  "ECHO:GLOS-COM2-PROBE" in echo, "%s, host got %r" % (st, echo.strip()[:40]))


def check_himemx():
    st, serial = job("selftest-himemx-hello", "--boot-cfg", "himemx",
                     "--file", "build/ow/dos/XMSINFO.COM=/HX/XMSINFO.COM", "--exe", "build/ow/dos/HELLO.EXE",
                     "--pre", "XMSINFO")
    line = next((l for l in serial.split("\n") if l.startswith("HX-XMS")), "no HX-XMS line")
    ok = report("himemx-hello", st == "PASS" and "ver=0300" in line and "doshigh=1" in line,
                "HELLO %s; %s" % (st, line))
    st, _ = job("selftest-himemx-stackpg", "--boot-cfg", "himemx", "--exe", "build/djgpp/STACKPG.EXE")
    return report("himemx-stackpg", st == "PASS", "STACKPG %s" % st) and ok


# CR4 as V86TEST's case A reports it, per profile.
CPU_CR4 = {"bf6": "bits=vme,pvi,tsd,de,pse,mce,pge,pce,osfxsr,", "486dx4": "bits=vme,pvi,", "486dx2": "cr4=absent"}


def known_86box():
    known = {}
    for line in open(os.path.join(ROOT, "tests/cpu/known-86box.txt")):
        if line.strip() and not line.startswith("#"):
            name, patch = line.split()[:2]
            known[name] = patch
    return known


def check_cpu():
    ok = True
    known = known_86box()
    for m in ("bf6", "486dx2", "486dx4"):
        st, serial = job("selftest-cpu-%s" % m, "--machine", m, "--exe", "build/ow/dos/V86TEST.EXE",
                         "--file", "build/cpu/V86PM.BIN=/HX/V86PM.BIN")
        tests = {}
        for l in serial.split("\n"):
            if l.startswith("HX-TEST cpu-"):
                parts = l.split(" ", 3)
                tests[parts[1][4:]] = (parts[2], parts[3] if len(parts) > 3 else "")
        failed = sorted(n for n, (r, _) in tests.items() if r == "FAIL")
        unknown = [n for n in failed if n not in known]
        fixed = sorted(n for n in known if n in tests and tests[n][0] == "PASS")
        cr4 = tests.get("A-features", ("?", ""))[1]
        good = "HX-DONE" in serial and "monitor" in tests and not unknown and CPU_CR4[m] in cr4
        detail = "%d cases, %d known 86Box deviations" % (len(tests), len(failed) - len(unknown))
        if unknown:
            detail += ", unexpected failures: " + " ".join(unknown)
        if fixed:
            detail += ", listed but now passing: " + " ".join(fixed)
        if CPU_CR4[m] not in cr4:
            detail += ", CR4 %r (want %r)" % (cr4, CPU_CR4[m])
        ok &= report("cpu-%s" % m, good, detail)
    return ok


# The chip each PCI card is, as PROBE's pci case names it.
PCI_CHIP = {"g200pci": "MGA-G200 (PCI) id=0520", "g400pci": "MGA-G400/G450 id=0525 rev=05", "g450pci": "MGA-G450 id=0525 rev=85"}


def check_pci():
    ok = True
    for c, chip in sorted(PCI_CHIP.items()):
        st, serial = job("selftest-pci-%s" % c, "--machine", "486dx2", "--card", c, "--exe", "build/djgpp/PROBE.EXE")
        cases = [l.split(" ", 3)[1:] for l in serial.split("\n") if l.startswith("HX-TEST ")]
        failed = [n for n, r, *_ in cases if r != "PASS"]
        pci = next((l for l in serial.split("\n") if l.startswith("HX-TEST pci ")), "no pci case")
        good = st == "PASS" and chip in pci and len(cases) >= 7 and not failed
        ok &= report("pci-%s" % c, good, "PROBE %s, %d cases%s; %s" % (st, len(cases), ", failed: " + " ".join(failed)
                                                                      if failed else "", pci[len("HX-TEST pci "):]))
    return ok


def check_rtc():
    ok = True
    for m in ("bf6", "486dx2"):
        st, serial = job("selftest-rtc-%s" % m, "--machine", m, "--file", "build/ow/dos/RTCSET.COM=/HX/RTCSET.COM",
                         "--cmd", "SERSAY HX-START rtc", "--cmd", "RTCSET", "--cmd", "SERSAY HX-DONE 0")
        got = dict((l.split()[1], dict(f.split("=") for f in l.split()[2:]))
                   for l in serial.split("\n") if l.startswith("HX-RTC "))
        b, a = got.get("before", {}), got.get("after", {})
        good = st == "PASS" and b and a and a["a"] == b["a"] and a["b"] == b["b"] and a["year"] == a["set"]
        ok &= report("rtc-%s" % m, good, "%s: A %s->%s, B %s->%s, year %s->%s (set %s), century %s->%s" % (
            st, b.get("a"), a.get("a"), b.get("b"), a.get("b"), b.get("year"), a.get("year"), a.get("set"),
            b.get("cent"), a.get("cent")))
    return ok


def hazards_of(name):
    try:
        return json.load(open(os.path.join(OUT, name, "result.json"))).get("mga_hazards", {})
    except (OSError, ValueError):
        return None


def check_hazards():
    ok = True
    exe = ("--card", "g400", "--exe", "build/djgpp/WARPTRI.EXE")
    st, serial = job("selftest-hazards-none", *exe)
    hz = hazards_of("selftest-hazards-none")
    ok &= report("hazards-none", st == "PASS" and hz == {}, "WARPTRI %s, hazards %s" % (st, hz))
    st, serial = job("selftest-hazards", *exe, "--args", "hazards")
    hz = hazards_of("selftest-hazards") or {}
    got = dict((k, v["count"]) for k, v in hz.items())
    want = {"warp-inflight": 2, "warp-opmode": 1, "rfw": 1, "tlut": 1}
    ok &= report("hazards", st == "PASS" and got == want, "WARPTRI hazards %s, %s (want %s)" % (st, got, want))
    st, serial = job("selftest-hazards-agp", *exe, "--args", "agp=2 agpdraw cached")   # (AGP from cached rings: the hazard)
    hz = hazards_of("selftest-hazards-agp") or {}
    on = next((l for l in serial.split("\n") if l.startswith("HX-STAT agp on:")), "")
    good = st == "PASS" and set(hz) == {"agp-cached"} and on.endswith("card cmd=1f000302")
    ok &= report("hazards-agp", good, "WARPTRI agp=2 cached %s, %s; %s" % (st, sorted(hz), on[len("HX-STAT "):] or "AGP not on"))
    st, serial = job("selftest-hazards-probe", "--card", "g400", "--exe", "build/djgpp/PROBE.EXE")
    pci = next((l for l in serial.split("\n") if l.startswith("HX-TEST pci ")), "no pci case")
    ok &= report("hazards-probe", st == "PASS" and " rev=05 " in pci, "PROBE %s: %s" % (st, pci[len("HX-TEST pci "):]))
    return ok


def check_umbpci():
    umb = ("--file", "build/ow/dos/UMBINFO.COM=/HX/UMBINFO.COM", "--exe", "build/ow/dos/HELLO.EXE", "--pre", "UMBINFO")
    out = {}
    for cfg in ("default", "umbpci"):
        st, serial = job("selftest-umbpci-" + cfg, "--boot-cfg", cfg, *umb)
        line = next((l for l in serial.split("\n") if l.startswith("HX-UMB")), "")
        f = dict(kv.split("=") for kv in line.split()[1:]) if line else {}
        out[cfg] = (st, line, int(f.get("free", "-1")))
    good = out["default"][0] == "PASS" and out["default"][2] == 0 and out["umbpci"][0] == "PASS" and \
        out["umbpci"][2] > 32768
    return report("umbpci", good, "default %s %s; umbpci %s %s" % (out["default"][0], out["default"][1] or "no HX-UMB",
                                                                    out["umbpci"][0], out["umbpci"][1] or "no HX-UMB"))


def check_clock():
    st, serial = job("selftest-clock", "--exe", "build/djgpp/CLOCKMON.EXE", "--args", "60")
    stat = next((l for l in serial.split("\n") if l.startswith("HX-STAT clock ")), "")
    f = dict(kv.split("=") for kv in stat.split()[2:])
    good = st == "PASS" and f.get("backs") == "0"
    return report("clock", good, "CLOCKMON %s: %s" % (st, stat[len("HX-STAT clock "):] or "no clock line"))


def check_g450mem():
    """Local patch 0129: an emulated G450's VRAM after a soft reset, as bench F's card (RetroGPU
    docs/g450-memory.md). With the HAL's memory set-up nothing changes and no hazard is reported; with it skipped
    (MODETEST noreinit) VRAM decays and g450-memory is reported. Needs bench F's BIOS in the local ROM set (its PInS
    v5 block carries the set-up's values): skipped without it."""
    rom = os.path.join(os.environ.get("BOX86_DIR", os.path.join(os.environ.get("MGA_CACHE",
                       os.path.expanduser("~/.cache/mga-glide")), "86box")), "roms/video/matrox/mgaglide/g450-benchf.bin")
    if not os.path.exists(rom):
        return report("g450-memory", True, "skipped: no %s (bench F's BIOS, local only)" % rom)
    exe = ("--card", "g450", "--exe", "build/djgpp/MODETEST.EXE")
    out = {}
    for name, args in (("fixed", "hold=1 resets=2 decay=3"), ("broken", "hold=1 resets=1 noreinit decay=3")):
        st, serial = job("selftest-g450mem-" + name, *exe, "--args", args)
        last = [l for l in serial.split("\n") if l.startswith("HX-STAT modetest decay t=3s")]
        changed = int(last[-1].split("changed=")[1].split()[0]) if last else -1
        hz = hazards_of("selftest-g450mem-" + name) or {}
        out[name] = (st, changed, sorted(hz))
    good = out["fixed"][0] == "PASS" and out["fixed"][1] == 0 and out["fixed"][2] == [] and \
        out["broken"][0] == "PASS" and out["broken"][1] > 100000 and out["broken"][2] == ["g450-memory"]
    return report("g450-memory", good, "with the set-up %s, %d changed, hazards %s; without %s, %d changed, hazards %s" % (
        out["fixed"] + out["broken"]))


def check_g100vram():
    """Local patch 0130: an emulated G100 with less memory than its 16 MB aperture, as bench-g400's real one (4 MB,
    2026-11-18). Past its memory a write is lost and a read gives the last value written (VRAMBUS: as the real card
    read back), and the HAL's VRAM probe still finds 4 MB (it once read back its own write there and found 16)."""
    st, serial = job("selftest-g100vram", "--card", "g100", "--g100-mb", "4", "--exe", "build/djgpp/VRAMBUS.EXE",
                     "--args", "6")
    probe = re.search(r"vrambus card=.* probe=(\d+)", serial)
    rows = re.findall(r"vrambus off=([0-9a-f]+) wrote=([0-9a-f]+) now=([0-9a-f]+) drained=[0-9a-f]+ after-other=([0-9a-f]+) "
                      r"unwritten=([0-9a-f]+) fb0=([0-9a-f]+)", serial)
    past = [r for r in rows if int(r[0], 16) >= 4 << 20]
    good = st == "PASS" and probe is not None and int(probe.group(1)) == 4 << 20 and len(past) >= 2 and \
        all(r[2] == r[1] and r[3] == "5a5a5a5a" and r[4] == "5a5a5a5a" and r[5] == "11111111" for r in past)
    return report("g100-vram", good, "probe %s; past 4 MB: %s" % (
        probe.group(1) if probe else "?", "; ".join("%s now=%s after=%s" % (r[0], r[2], r[3]) for r in past) or "no rows"))


def check_g100stall():
    """Local patch 0132: a real G100's engine stalls on a textured trapezoid that draws nothing with its spans left of
    the clip (bench-g400's PC, 2026-11-18; with fog the PC hung). 86Box's G100 reports it as the textrap-empty hazard.
    FOGPROBE draws t10's fifth triangle (its top row an empty span on the screen, the rest wholly left of it): with the
    HAL's cull (mga.textrap_empty_stall) nothing is sent and nothing reported; with nocull the hazard."""
    out = {}
    for name, args in (("cull", "t10only5"), ("nocull", "t10only5 nocull")):
        st, serial = job("selftest-g100stall-" + name, "--card", "g100", "--exe", "build/djgpp/FOGPROBE.EXE",
                         "--args", args)
        m = re.search(r"fogprobe step=t10only5 status=.* traps=(\d+) culled=(\d+)", serial)
        out[name] = (st, m.group(1) if m else "?", m.group(2) if m else "?", sorted(hazards_of("selftest-g100stall-" + name) or {}))
    good = out["cull"] == ("PASS", "0", "1", []) and out["nocull"] == ("PASS", "1", "0", ["textrap-empty"])
    return report("g100-stall", good, "culled: %s traps=%s culled=%s hazards %s; nocull: %s traps=%s culled=%s hazards %s" % (
        out["cull"] + out["nocull"]))


CHECKS = {"486": check_486, "vbe": check_vbe, "net": check_net, "com2": check_com2, "himemx": check_himemx,
          "cpu": check_cpu, "pci": check_pci, "rtc": check_rtc, "hazards": check_hazards, "clock": check_clock,
          "umbpci": check_umbpci, "g450mem": check_g450mem, "g100vram": check_g100vram, "g100stall": check_g100stall}


def main():
    names = sys.argv[1:] or list(CHECKS)
    bad = [n for n in names if not CHECKS[n]()]
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
