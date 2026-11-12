#!/usr/bin/env python3
"""Loop A: run one DOS test program in 86Box (G100 + optional Voodoo).

Runs inside the dev container. Example:
  run.py --name hello --exe build/ow/dos/HELLO.EXE --args "--fail"
  run.py --name t02 --exe build/ow/dos/T02.EXE --ovl build/ow/GLIDE2X.OVL
  run.py --name q2 --games-file G.json --game quake2 --file X.DXE=D:/QUAKE2/BASEQ2/X.DXE ...

Result directory out/<name>/: serial.log, 86box.log, stderr.log, files/
(the guest's C:\\OUT), *.png, result.json, status.

Statuses: PASS FAIL TIMEOUT HANG GUEST-EXC CRASH EMU-FATAL SETUP-ERROR.
"""
import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import hostio  # noqa: E402
import png  # noqa: E402

CACHE = os.environ.get("MGA_CACHE", os.path.expanduser("~/.cache/mga-glide"))
BOX86_DIR = os.environ.get("BOX86_DIR", os.path.join(CACHE, "86box"))
WATCOM = os.environ.get("WATCOM", os.path.expanduser("~/.local/opt/watcom-20260901"))
DJGPP_PREFIX = os.environ.get("DJGPP_PREFIX", os.path.expanduser("~/.local/opt/djgpp-gcc1220"))
# DPMI host for DJGPP programs (fetched by `make setup-djgpp`); found via PATH.
CWSDPMI = os.path.join(DJGPP_PREFIX, "dos", "CWSDPMI.EXE")



# Video cards the harness can fit: 86Box internal name, config section. The
# Matrox cards are AGP in 86Box, so only the BF6 takes them, except for the
# PCI G200, G400 and G450 of local patch 0116 (GLOS D38), which any profile
# takes; "vbe" is a generic VESA 2.0 card with a linear framebuffer; "virge" is
# the S3 ViRGE/DX (86C375, PCI, generic BIOS, 4 MB): bench C's card, the first
# non-Matrox back end (gl/docs/backends.md).
CARDS = {
    "g100": ("productiva_g100", "Matrox Productiva G100"),
    "g200": ("millennium_g200", "Matrox Millennium G200 (MGA-Glide emulation)"),
    "g400": ("millennium_g400", "Matrox Millennium G400 (MGA-Glide emulation)"),
    "g450": ("millennium_g450", "Matrox Millennium G450 (MGA-Glide emulation)"),
    "g200pci": ("millennium_g200_pci", "Matrox Millennium G200 PCI (MGA-Glide emulation)"),
    "g400pci": ("millennium_g400_pci", "Matrox Millennium G400 PCI (MGA-Glide emulation)"),
    "g450pci": ("millennium_g450_pci", "Matrox Millennium G450 PCI (MGA-Glide emulation)"),
    "vbe": ("s3_trio64v2dx_pci", "S3 Trio64V2/DX PCI"),     # "Generic" BIOS: S3 86C775 with VBE 2.0
    "virge": ("virge_dx_pci", "S3 ViRGE/DX PCI"),           # BIOS variant virge375_pci (the default)
}
MATROX = ("g100", "g200", "g400", "g450")                   # (AGP)
MATROX_PCI = ("g200pci", "g400pci", "g450pci")

# Machine profiles (--machine): board, CPU, the board's own config section,
# the default card, and the NVRAM cache (CMOS settings saved after the first
# clean run, so later runs skip "press F1").
PROFILES = {
    "bf6": dict(machine="bf6", cpu_family="pentium2_deschutes", cpu_speed="350000000", cpu_multi="3.5",
                extra="", card=None, nvr="nvr-bf6"),
    # Shuttle HOT-433A (UMC 8881, PCI) with its AwardBIOS 4.51PG.
    "486dx2": dict(machine="hot433a", cpu_family="i486dx2", cpu_speed="66666666", cpu_multi="2",
                   extra="[Shuttle HOT-433A]\nbios = hot433a_v451pg\n\n", card="vbe", nvr="nvr-486dx2"),
    # Intel iDX4-100: an SL-enhanced 486 with CR4.VME/PVI.
    "486dx4": dict(machine="hot433a", cpu_family="idx4", cpu_speed="100000000", cpu_multi="3",
                   extra="[Shuttle HOT-433A]\nbios = hot433a_v451pg\n\n", card="vbe", nvr="nvr-486dx4"),
}


_slot = None


def vm_slot():
    """LOOPA_MAX_VMS=N: at most N emulators at once on this host, whatever
    started them (every run.py, in any session or container: the locks are
    files in the shared cache). Waits for a free slot and holds it until
    this process ends. Unset or 0: no limit."""
    global _slot
    n = int(os.environ.get("LOOPA_MAX_VMS", "0") or 0)
    if n <= 0 or _slot is not None:
        return
    import fcntl
    d = os.path.join(CACHE, "loopa-slots")
    os.makedirs(d, exist_ok=True)
    while True:
        for i in range(n):
            f = open(os.path.join(d, "slot%02d" % i), "a")
            try:
                fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                _slot = f
                return
            except OSError:
                f.close()
        time.sleep(2)


def resolve_profile(a):
    """Fill in --card from the profile and refuse a card the board can't take."""
    prof = PROFILES[getattr(a, "machine", "bf6")]
    if a.card is None:
        a.card = prof["card"] or os.environ.get("MGA_CARD", "g100")
    if a.card in MATROX and prof["machine"] != "bf6":
        raise RuntimeError("--card %s: 86Box's Matrox cards are AGP; --machine %s has none (use --card vbe)"
                           % (a.card, a.machine))
    return prof

def sh(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


class Disk:
    """mtools access to a partitioned hard-disk image."""

    def __init__(self, img, tmp, letter="c"):
        self.letter = letter
        self.rc = os.path.join(tmp, "mtoolsrc.%s" % letter)
        with open(self.rc, "w") as f:
            f.write('drive %s: file="%s" partition=1\nmtools_skip_check=1\n' % (letter, img))
        self.env = dict(os.environ, MTOOLSRC=self.rc)

    def mkdir(self, path):
        subprocess.run(["mmd", "-D", "s", self.letter + ":" + path], env=self.env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def put(self, src, dst):
        sh(["mcopy", "-o", "-D", "o", src, self.letter + ":" + dst], env=self.env)

    def get_dir(self, src, dst):
        os.makedirs(dst, exist_ok=True)
        subprocess.run(["mcopy", "-s", "-n", "-o", self.letter + ":" + src + "/*", dst + "/"], env=self.env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def start_xvfb(errf):
    """Start a private Xvfb on a free display; returns (process, ':N')."""
    r, w = os.pipe()
    x = subprocess.Popen(["Xvfb", "-displayfd", str(w), "-screen", "0", "1280x1024x24", "-nolisten", "tcp"],
                         pass_fds=(w,), stdout=errf, stderr=errf, start_new_session=True)
    os.close(w)
    num = b""
    while not num.endswith(b"\n"):
        c = os.read(r, 1)
        if not c:
            break
        num += c
    os.close(r)
    if not num.strip():
        raise RuntimeError("Xvfb did not start")
    return x, ":" + num.decode().strip()


CTMOUSE_URL = "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/ctmouse.zip"
CTMOUSE_SHA256 = "fd47069fb3d9559604dcaef34ca4f3705a7f2f2cff3e4b205e361b79f10a8200"


def ctmouse(tmp):
    """CuteMouse 2.1 (GPL) from FreeDOS 1.4's base repository, for --mouse."""
    import zipfile
    z = os.path.join(tmp, "ctmouse.zip")
    sh([os.path.join(ROOT, "tools/setup/fetch.sh"), CTMOUSE_URL, CTMOUSE_SHA256, z])
    exe = os.path.join(tmp, "CTMOUSE.EXE")
    open(exe, "wb").write(zipfile.ZipFile(z).read("BIN/CTMOUSE.EXE"))
    return exe


def net_dos(d, tmp):
    """--net-dos: the Crynwr NE2000 packet driver and mTCP's DHCP and NC (the
    versions Loop B's bench PCs use) in C:\\PKTDRV and C:\\MTCP, with a TCP.CFG
    for the packet driver at INT 60h. Loading them is up to the job."""
    import re
    import zipfile
    pins = dict(re.findall(r"^(\w+)\s*:=\s*(\S+)", open(os.path.join(ROOT, "tools/setup/versions.mk")).read(), re.M))
    fetch = os.path.join(ROOT, "tools/setup/fetch.sh")
    sh([fetch, pins["MTCP_URL"], pins["MTCP_SHA256"], os.path.join(tmp, "mtcp.zip")])
    sh([fetch, pins["CRYNWR_URL"], pins["CRYNWR_SHA256"], os.path.join(tmp, "crynwr.zip")])
    d.mkdir("/MTCP")
    d.mkdir("/PKTDRV")
    mz = zipfile.ZipFile(os.path.join(tmp, "mtcp.zip"))
    for name in ("dhcp.exe", "nc.exe"):
        f = os.path.join(tmp, name.upper())
        open(f, "wb").write(mz.read(name))
        d.put(f, "/MTCP/" + name.upper())
    f = os.path.join(tmp, "NE2000.COM")
    open(f, "wb").write(zipfile.ZipFile(os.path.join(tmp, "crynwr.zip")).read("DRIVERS/CRYNWR/NE2000.COM"))
    d.put(f, "/PKTDRV/NE2000.COM")
    f = os.path.join(tmp, "TCP.CFG")
    open(f, "wb").write(b"PACKETINT 0x60\r\n")
    d.put(f, "/MTCP/TCP.CFG")


def dos_bat(lines):
    return ("\r\n".join(["@ECHO OFF"] + lines) + "\r\n").encode("ascii")


def golden(variant="default"):
    """The golden boot floppy and C: images: mkgolden.sh's, or a variant of
    them (--boot-cfg; mkgolden-variant.sh)."""
    cmd = [os.path.join(HERE, "mkgolden.sh")] if variant == "default" else \
        [os.path.join(HERE, "mkgolden-variant.sh"), variant]
    out = subprocess.run(cmd, check=True, stdout=subprocess.PIPE, text=True).stdout.strip().splitlines()[-1]
    return out


def golden_key():
    """The key mkgolden.sh files its images under (the script plus dos/*),
    computed without building anything (for --emit-config)."""
    import hashlib
    h = hashlib.sha256(open(os.path.join(HERE, "mkgolden.sh"), "rb").read())
    for fn in sorted(os.listdir(os.path.join(HERE, "dos"))):
        h.update(open(os.path.join(HERE, "dos", fn), "rb").read())
    return h.hexdigest()[:12]


def golden_variant_key(variant):
    """mkgolden-variant.sh's key for VARIANT, computed the same way."""
    import hashlib
    import re
    pins = dict(re.findall(r"^(\w+)\s*:=\s*(\S+)", open(os.path.join(ROOT, "tools/setup/versions.mk")).read(), re.M))
    h = hashlib.sha256(("golden-%s\n" % golden_key()).encode())
    h.update(open(os.path.join(HERE, "mkgolden-variant.sh"), "rb").read())
    vdir = os.path.join(HERE, "dos-" + variant)
    for fn in sorted(os.listdir(vdir)):
        h.update(open(os.path.join(vdir, fn), "rb").read())
    h.update((pins["HIMEMX_SHA256"] + "\n").encode())
    return "%s-%s" % (variant, h.hexdigest()[:12])


def joystick_config(kind):
    """[Input devices] lines for --joystick: the virtual joystick (local patch
    0105) is platform joystick 1 under Xvfb, which has no real ones. The
    monitor's "joy axis N" drives the game port's axis N (bit N of port
    0x201): 86Box's 4-axis sticks read the port's axes 2 and 3 from their
    axes 3 and 2 (rudder, throttle), so those two are mapped crosswise."""
    if kind == "none":
        return ""
    lines = "joystick_type = %s\njoystick_0_nr = 1\n" % kind
    if kind.startswith("4axis"):
        lines += "joystick_0_axis_2 = 3\njoystick_0_axis_3 = 2\n"
    return lines


def build_config(vm, a, serial, cimg, bootimg, extra_hdd):
    path = os.path.join(vm, "86box.cfg")
    open(path, "w").write(config_text(a, serial, cimg, bootimg, extra_hdd))
    return path


def config_text(a, serial, cimg, bootimg, extra_hdd, tail=""):
    tpl = open(os.path.join(HERE, "86box.cfg.in")).read()
    prof = PROFILES[getattr(a, "machine", "bf6")]
    subst = {
        "@MACHINE@": prof["machine"],
        "@CPU_FAMILY@": prof["cpu_family"],
        "@CPU_SPEED@": prof["cpu_speed"],
        "@CPU_MULTI@": prof["cpu_multi"],
        "@MACHINE_EXTRA@": prof["extra"],
        "@DYNAREC@": str(getattr(a, "dynarec", 1)),
        # COM2 (--com2) is 86Box's named-pipe device on the bridge's pty.
        "@COM2@": "1\nserial2_device = pipe" if getattr(a, "com2", False) else "0",
        # Sections appended at the end: the network card (--net), COM2's pipe.
        "@TAIL@": tail,
        "@RENDERER@": "sdl_software",
        "@VOODOO@": "1" if a.voodoo else "0",
        "@VOODOO_RECOMPILER@": str(a.voodoo_recompiler),
        "@VOODOO_THREADS@": str(a.voodoo_threads),
        "@G100_MB@": str(4 if a.card in ("vbe", "virge") else max(a.g100_mb, 16) if a.card in ("g400", "g450", "g400pci", "g450pci")
                         else a.g100_mb),
        # vpc.py passes a minimal options object: these have defaults.
        "@MEM_KB@": str(getattr(a, "mem", 64) * 1024),
        "@GFXCARD@": CARDS[a.card][0],
        "@GFXNAME@": CARDS[a.card][1],
        "@SNDCARD@": a.sound or "none",
        "@MOUSE@": getattr(a, "mouse", "none"),
        # The virtual joystick (local patch 0105) is platform joystick 1 under
        # Xvfb, which has no real ones; 86Box adds a standalone game port at
        # 0x201 when no sound card brings one. Axes and buttons map 1:1.
        "@JOYSTICK@": joystick_config(getattr(a, "joystick", "none")),
        "@SERIAL@": serial,
        "@CIMG@": cimg,
        "@CYLS@": str(getattr(a, "hdd_cyls", 1000)),
        "@BOOTIMG@": bootimg,
        "@EXTRA_HDD@": extra_hdd,
    }
    for k, v in subst.items():
        tpl = tpl.replace(k, v)
    return tpl


def run_bat_lines(a, exe_name, game):
    """C:\\RUN.BAT, which the boot floppy's AUTOEXEC.BAT calls."""
    if getattr(a, "net_dos", False):
        # mTCP and the packet drivers (--net-dos); loading them is the job's.
        run_lines = ["SET PATH=C:\\HX;C:\\MTCP;C:\\PKTDRV;A:\\FREEDOS\\BIN", "SET MTCPCFG=C:\\MTCP\\TCP.CFG"]
    else:
        run_lines = ["SET PATH=C:\\HX;A:\\FREEDOS\\BIN"]
    run_lines += ["C:", "CD \\TEST", "SERSAY HX-BOOT loop=A test=%s" % a.name]
    wrap = (getattr(a, "wrap", "") + " ") if getattr(a, "wrap", "") else ""
    if a.mouse != "none":
        run_lines += ["CTMOUSE"]
    run_lines += a.pre
    if a.cmd:
        run_lines += a.cmd
    elif game:
        run_lines += ["D:", "CD \\" + game["cwd"]] + [wrap + l + (" " + a.args if a.args else "") for l in game["run"]]
        run_lines += ["C:", "SERSAY HX-GAME-EXIT"]
    else:
        run_lines += ["%sC:\\TEST\\%s %s" % (wrap, exe_name, a.args or "")]
    run_lines += ["VMODE", "SERSAY HX-EXIT program returned without ending the run",
                  "UTEXIT 124"]
    return run_lines


def emit_config(a):
    """--emit-config: what a run would generate (86box.cfg with placeholder
    paths, RUN.BAT, the golden image key) without starting anything.
    tools/loopa/cfgcheck.py compares this with tools/loopa/ref/."""
    if a.joystick == "none" and ":joy:" in a.keys:
        a.joystick = "4axis_4button"
    resolve_profile(a)
    game = json.load(open(a.games_file))[a.game] if a.game else None
    exe_name = os.path.basename(a.exe).upper() if a.exe else None
    tail = ""
    if a.net:
        fw = hostio.parse_forwards(a.net_fwd or ["22"])
        tail += hostio.net_config(a.net, [(h if h else "@HOSTPORT%d@" % i, g) for i, (h, g) in enumerate(fw)])
    if a.com2:
        tail += hostio.com2_config("@COM2PTY@")
    out = ["# 86box.cfg", config_text(a, "@SERIAL@", "@CIMG@", "@BOOTIMG@", "", tail),
           "# RUN.BAT"] + run_bat_lines(a, exe_name, game) + \
        ["# golden " + (golden_key() if a.boot_cfg == "default" else golden_variant_key(a.boot_cfg))]
    sys.stdout.write("\n".join(out) + "\n")
    return 0


def parse_serial(text):
    info = {"tests": [], "images": [], "stats": [], "done": None, "start": None, "lines": 0}
    for line in text.replace("\r", "").split("\n"):
        if not line:
            continue
        info["lines"] += 1
        if line.startswith("HX-START"):
            info["start"] = line
        elif line.startswith("HX-TEST "):
            parts = line.split(" ", 3)
            info["tests"].append({"name": parts[1], "result": parts[2] if len(parts) > 2 else "?",
                                  "detail": parts[3] if len(parts) > 3 else ""})
        elif line.startswith("HX-IMG "):
            info["images"].append(line[7:])
        elif line.startswith("HX-STAT "):
            info["stats"].append(line[8:])
        elif line.startswith("HX-DONE "):
            try:
                info["done"] = int(line.split()[1])
            except (IndexError, ValueError):
                info["done"] = -1
    return info


def hdd_disk(a):
    """--hdd: the image as C: (used in place: the guest writes to it), its cylinders from its size (63 sectors
    by 16 heads, as the golden C: is), and no floppy, so the BIOS boots the disk's own MBR. Nothing is put on
    it: the job's files, RUN.BAT and the harness's tools belong to the golden disk."""
    if a.exe or a.cmd or a.file or a.pre or a.ovl or a.game or a.net_dos:
        raise RuntimeError("--hdd boots the disk as it is: no --exe, --cmd, --file, --pre, --ovl, --game or --net-dos")
    cimg = os.path.abspath(a.hdd)
    size = os.path.getsize(cimg)
    if not size or size % (63 * 16 * 512):
        raise RuntimeError("--hdd %s: %d bytes, not a whole number of cylinders (63 x 16 x 512)" % (cimg, size))
    a.hdd_cyls = size // (63 * 16 * 512)
    return cimg, "", ""


def scratch_cyls(mb):
    """--scratch-hdd MB: whole cylinders of 63 sectors by 16 heads (516,096 bytes), 1 to 1023 (the BIOS's CHS
    limit, about 504 MB)."""
    cyl = int(mb) * 1048576 // (63 * 16 * 512)
    if not 1 <= cyl <= 1023:
        raise RuntimeError("--scratch-hdd %s: 1 to 504 MB" % mb)
    return cyl


FATAL_RE = re.compile(r"(^|\s)(FATAL|fatal error|Fatal error)", re.M)


def run(a):
    if a.joystick == "none" and ":joy:" in a.keys:
        a.joystick = "4axis_4button"
    out = os.path.abspath(a.out or os.path.join(ROOT, "out", a.name))
    shutil.rmtree(out, ignore_errors=True)
    vm = os.path.join(out, "vm")
    os.makedirs(vm)
    tmp = tempfile.mkdtemp(prefix="loopa-")
    result = {"name": a.name, "exe": a.exe, "args": a.args, "ovl": a.ovl, "status": "SETUP-ERROR"}
    try:
        prof = resolve_profile(a)
        result.update(machine=a.machine, card=a.card, boot_cfg=a.boot_cfg)
        d = None
        if a.floppy and not a.hdd:
            raise RuntimeError("--floppy goes with --hdd (the golden boot floppy is the harness's own)")
        if a.hdd:                               # a bench PC's own disk (docs/loops.md): booted as it is
            cimg, bootimg, extra = hdd_disk(a)
            if a.floppy:                        # ... with a floppy of the job's own in A:, which the BIOS starts
                bootimg = os.path.join(vm, "boot.img")      # from (GLOS's setup floppy and a blank disk)
                shutil.copyfile(a.floppy, bootimg)
        else:
            gold = golden(a.boot_cfg)
            bootimg = os.path.join(vm, "boot.img")
            cimg = os.path.join(vm, "c.img")
            shutil.copyfile(os.path.join(gold, "boot.img"), bootimg)
            sh(["cp", "--sparse=always", os.path.join(gold, "c.img"), cimg])
            d = Disk(cimg, tmp)
            for t in ("UTEXIT.COM", "SERSAY.COM", "WAITSEC.COM", "REBOOT.COM", "VMODE.COM", "KEYWAIT.COM", "VECCHK.COM", "SBCHK.COM"):
                d.put(os.path.join(ROOT, "build/ow/dos", t), "/HX/" + t)
            d.put(os.path.join(WATCOM, "binw/dos4gw.exe"), "/HX/DOS4GW.EXE")
            if os.path.exists(CWSDPMI):
                d.put(CWSDPMI, "/HX/CWSDPMI.EXE")
            if a.mouse != "none":
                d.put(ctmouse(tmp), "/HX/CTMOUSE.EXE")
            if a.net_dos:
                net_dos(d, tmp)
            d.mkdir("/TEST")
            exe_name = os.path.basename(a.exe).upper() if a.exe else None
            if a.exe:
                d.put(a.exe, "/TEST/" + exe_name)
            game = None
            extra = ""
            if a.game:
                game = json.load(open(a.games_file))[a.game]
                fix = os.environ.get("FIXTURES_DIR", os.path.join(CACHE, "fixtures"))
                # The D: image's size comes from the game (63 x 16 x cylinders, at most 1023:
                # the BIOS CHS limit, about 504 MB).
                cyl = int(game.get("cylinders", 406))
                gimg_src = subprocess.run([os.path.join(ROOT, "tools/games/mkimage.sh"), a.game,
                                           os.path.join(fix, game["fixture"]), game["dir"], str(cyl)],
                                          check=True, stdout=subprocess.PIPE, text=True).stdout.strip().splitlines()[-1]
                gimg = os.path.join(vm, "game.img")
                sh(["cp", "--sparse=always", gimg_src, gimg])
                extra = ("hdd_02_parameters = 63, 16, %d, 0, ide\nhdd_02_fn = %s\n"
                         "hdd_02_ide_channel = 0:1\n" % (cyl, gimg))
                gd = Disk(gimg, tmp, "d")
                if a.sound is None:
                    a.sound = game.get("sound", "")
            for spec in a.file:
                src, dst = spec.split("=", 1) if "=" in spec else (spec, "/TEST/" + os.path.basename(spec).upper())
                dst = dst.replace("\\", "/")
                disk = d
                if dst[:2].upper() == "D:":             # onto the game disk (--game)
                    if not game:
                        raise RuntimeError("--file %s: D: needs --game" % spec)
                    disk, dst = gd, dst[2:]
                parts = dst.strip("/").split("/")[:-1]
                for i in range(1, len(parts) + 1):
                    disk.mkdir("/" + "/".join(parts[:i]))
                disk.put(src, dst)
            if a.ovl:
                import hashlib
                dst = a.ovl_dst or ("D:\\" + game["ovl"] if game else "C:\\TEST\\GLIDE2X.OVL")
                drive, path = dst[0].lower(), dst[2:].replace("\\", "/")
                (gd if drive == "d" else d).put(a.ovl, path)
                result["ovl_sha256"] = hashlib.sha256(open(a.ovl, "rb").read()).hexdigest()
                result["ovl_dst"] = dst
            run_lines = run_bat_lines(a, exe_name, game)
            rb = os.path.join(tmp, "RUN.BAT")
            open(rb, "wb").write(dos_bat(run_lines))
            d.put(rb, "/RUN.BAT")

        if a.scratch_hdd:                       # a blank disk for raw-sector tests (GLOS's DSKTORT), IDE 1:0
            cyl = scratch_cyls(a.scratch_hdd)
            simg = os.path.join(vm, "scratch.img")
            with open(simg, "wb") as f:
                f.truncate(63 * 16 * 512 * cyl)
            extra += "hdd_03_parameters = 63, 16, %d, 0, ide\nhdd_03_fn = %s\nhdd_03_ide_channel = 1:0\n" % (cyl, simg)
            result["scratch_hdd"] = {"cylinders": cyl, "sectors": 63 * 16 * cyl}

        serial = os.path.join(out, "serial.log")
        open(serial, "w").close()
        tail = ""
        ports = {}
        if a.net:
            fw = [(h or hostio.free_port(), g) for h, g in hostio.parse_forwards(a.net_fwd or ["22"])]
            tail += hostio.net_config(a.net, fw)
            ports.update(("net:%d" % g, h) for h, g in fw)
            result["net"] = {"card": a.net, "forwards": [{"host": h, "guest": g} for h, g in fw]}
        if a.com2:
            bridge = hostio.Com2Bridge(os.path.join(out, "com2.log"))
            bridge.start()
            tail += hostio.com2_config(bridge.path)
            ports["com2"] = bridge.port
            result["com2_port"] = bridge.port
            # Also now, for a debugger attaching while the job runs (GLOS's gdb test).
            open(os.path.join(out, "com2.port"), "w").write("%d\n" % bridge.port)
        path = os.path.join(vm, "86box.cfg")
        open(path, "w").write(config_text(a, serial, cimg, bootimg, extra, tail))
        cfg = path
        box = os.path.join(BOX86_DIR, "bin", "86Box")
        roms = os.path.join(BOX86_DIR, "roms")
        cmd = [box, "-P", vm, "-C", cfg, "-R", roms, "-N", "-L", os.path.join(out, "86box.log")]
        if os.environ.get("BOX86_GDB"):
            cmd = ["gdb", "-q", "-batch", "-ex", "handle SIGUSR1 SIGUSR2 SIGPIPE nostop noprint",
                   "-ex", "run", "-ex", "thread apply all bt 12", "--args"] + cmd
        nvr_cache = os.path.join(CACHE, "loopa", prof["nvr"])
        if os.path.isdir(nvr_cache):
            shutil.copytree(nvr_cache, os.path.join(vm, "nvr"), dirs_exist_ok=True)
        errf = open(os.path.join(out, "stderr.log"), "wb")
        vm_slot()                               # (LOOPA_MAX_VMS: wait for room before the clocks start)
        xvfb, display = start_xvfb(errf)
        env = dict(os.environ, BOX86_MONITOR="1", HOME=vm, XDG_CONFIG_HOME=vm,
                   SDL_AUDIODRIVER="dummy", DISPLAY=display)
        if a.joystick != "none":
            env["BOX86_VJOY"] = "1"             # local patch 0105: the monitor's "joy" command
        if a.wav:
            # 86Box plays through OpenAL Soft: its wave backend writes what the
            # guest's sound card produced. 86Box does not run in step with the
            # wall clock, so the file shows the right sounds with gaps, not
            # exact timing.
            conf = os.path.join(vm, "alsoft.conf")
            open(conf, "w").write("[general]\ndrivers = wave\nsample-type = int16\n[wave]\nfile = %s\n"
                                  % os.path.join(out, "audio.wav"))
            env.update(ALSOFT_CONF=conf, ALSOFT_DRIVERS="wave")
        t0 = time.time()
        p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=errf, stderr=errf, env=env)
        last_size, last_change, status, done_at = 0, time.time(), None, None
        f1_taps, next_f1 = 0, t0 + 6
        boot_at = t0 if a.hdd else None         # (--hdd: --shots from the start; the disk's DOS may say nothing)
        # --keys in segments: the first timed from boot, each later one from
        # when its @TEXT anchor shows in the serial log (searched after the
        # previous anchor's match), so a slow emulator cannot outrun a script.
        segments = [[None, None, []]]           # [anchor text, base time, [(seconds, command)]]
        for k in filter(None, a.keys.split(",")):
            if k.startswith("@"):
                segments.append([k[1:], None, []])
                continue
            parts = k.split(":")
            if parts[1] == "mouse":             # SECONDS:mouse:DX:DY[:BUTTONS]
                segments[-1][2].append((float(parts[0]), "mouse %s\n" % " ".join(parts[2:5])))
                continue
            if parts[1] == "joy":               # SECONDS:joy:axis|button:N:VALUE
                segments[-1][2].append((float(parts[0]), "joy %s\n" % " ".join(parts[2:5])))
                continue
            mode = parts[2] if len(parts) > 2 else "tap"
            segments[-1][2].append((float(parts[0]), "key %s %s\n" % (mode, parts[1])))
        for seg in segments:
            seg[2].sort()
        events = sorted((float(sh_t), "screenshot\n") for sh_t in filter(None, a.shots.split(",")))
        seg_i, anchor_pos, serial_text = 0, 0, ""
        sends = [hostio.parse_tcp_send(t) for t in a.tcp_send]
        senders = []
        steps = None
        if a.ssh_steps:                         # H7: SSH steps against the guest (GLOS's agent)
            if "net:22" not in ports:
                raise RuntimeError("--ssh-steps needs --net (with guest port 22 forwarded)")
            steps = hostio.SshSteps(hostio.parse_ssh_steps(a.ssh_steps), ports["net:22"], a.ssh_key, a.ssh_user,
                                    serial, out, os.path.dirname(os.path.abspath(a.ssh_steps)))
            steps.start()

        def console(cmd):
            try:
                p.stdin.write(cmd.encode())
                p.stdin.flush()
            except OSError:
                pass
        while True:
            rc = p.poll()
            size = os.path.getsize(serial)
            now = time.time()
            if size != last_size:
                last_size, last_change = size, now
                text = serial_text = open(serial, "rb").read().decode("latin-1")
                # Finished once every program started has reported HX-DONE
                # (a --cmd job can run several programs in turn).
                started = text.count("HX-START ") + text.count("DGL-START")
                finished = text.count("HX-DONE ") + text.count("DGL-EXIT")
                if finished and finished >= started:
                    done_at = done_at or now
                else:
                    done_at = None
            if rc is not None:
                break
            if boot_at is None and size:
                boot_at = now
            while events and boot_at is not None and now - boot_at >= events[0][0]:
                console(events.pop(0)[1])
            while seg_i < len(segments) and boot_at is not None:
                seg = segments[seg_i]
                if seg[1] is None:
                    if seg[0] is None:
                        seg[1] = boot_at
                    else:
                        at = serial_text.find(seg[0], anchor_pos)
                        if at < 0:
                            break
                        anchor_pos, seg[1] = at + len(seg[0]), now
                while seg[2] and now - seg[1] >= seg[2][0][0]:
                    console(seg[2].pop(0)[1])
                if seg[2]:
                    break
                seg_i += 1
            while sends and sends[0]["anchor"] in serial_text:
                # --tcp-send: once its anchor is on the serial line, in order.
                st = sends.pop(0)
                if st["target"] not in ports:
                    raise RuntimeError("--tcp-send %s: no such port (needs --net/--net-fwd or --com2)" % st["target"])
                t = hostio.TcpSend(ports[st["target"]], st["text"], os.path.join(out, "tcp-%d.txt" % len(senders)))
                t.start()
                senders.append(t)
            if last_size == 0 and now >= next_f1 and f1_taps < 12 and not a.hdd:   # (--hdd: the disk's own menus)
                # A fresh NVRAM stops the BIOS at "press F1 to continue".
                try:
                    p.stdin.write(b"key tap 0x3b\n")
                    p.stdin.flush()
                except OSError:
                    pass
                f1_taps += 1
                next_f1 = now + 3
            if done_at and now - done_at > 15:
                status = "NO-EXIT"
                break
            if now - t0 > a.timeout:
                status = "TIMEOUT"
                break
            if now - last_change > a.idle and now - t0 > a.boot_grace:
                status = "HANG"
                break
            if steps is not None and steps.failed:
                status = "FAIL"                 # an SSH step failed: no use waiting for the guest
                break
            time.sleep(0.25)
        if p.poll() is None and status in ("HANG", "TIMEOUT"):
            # Leave a picture of the emulated display for diagnosis.
            try:
                shots = os.path.join(vm, "screenshots")
                p.stdin.write(b"screenshot\n")
                p.stdin.flush()
                for _ in range(40):
                    time.sleep(0.25)
                    if os.path.isdir(shots) and os.listdir(shots):
                        time.sleep(0.5)
                        break
            except OSError:
                pass
        if p.poll() is None:
            try:
                p.stdin.write(b"exit\n")
                p.stdin.flush()
            except OSError:
                pass
            for _ in range(40):
                if p.poll() is not None:
                    break
                time.sleep(0.25)
            if p.poll() is None:
                os.kill(p.pid, signal.SIGKILL)
                p.wait()
        xvfb.terminate()
        try:
            xvfb.wait(timeout=5)
        except subprocess.TimeoutExpired:
            xvfb.kill()
        errf.close()
        for t in senders:
            t.join(timeout=10)
        if senders:
            result["tcp_send"] = [t.result for t in senders]
        if steps:
            steps.join(timeout=30)
            result["ssh_steps"] = steps.results
        if a.com2:
            bridge.stop()
        result["elapsed_s"] = round(time.time() - t0, 1)
        result["box_exit"] = p.returncode
        text = open(serial, "rb").read().decode("latin-1")
        info = parse_serial(text)
        result.update(info)
        stderr_text = open(os.path.join(out, "stderr.log"), "rb").read().decode("latin-1")
        result["mga_unsupported"] = sorted(set(re.findall(r"MGA-UNSUPPORTED: (.*)", stderr_text)))
        # 86Box's silicon checks (local patch 0124): each kind's count (the highest reported, so at least
        # this many) and its first message
        hazards = {}
        for kind, n, msg in re.findall(r"MGA-HAZARD: ([a-z-]+): #(\d+): (.*)", stderr_text):
            h = hazards.setdefault(kind, {"count": 0, "first": msg})
            h["count"] = max(h["count"], int(n))
        result["mga_hazards"] = hazards
        logpath = os.path.join(out, "86box.log")
        boxlog = open(logpath, "rb").read().decode("latin-1") if os.path.exists(logpath) else ""
        if status is None:
            if FATAL_RE.search(boxlog + stderr_text) and info["done"] is None:
                status = "EMU-FATAL"
            elif info["done"] is None and p.returncode in (0, 124) and ("MGL-EXIT frames=" in text or "DGL-EXIT frames=" in text):
                status = "PASS"        # a game ended by MGAGLIDE exit_after / DGL_EXIT_AFTER (no HX-DONE from games)
            elif info["done"] is None and p.returncode == 124:
                status = "GUEST-EXC"   # program died; RUN.BAT's fallback ended the run
            elif info["done"] is None:
                status = "CRASH"
            elif info["done"] == 0 and p.returncode in (0, 124):
                status = "PASS"        # 124: reported HX-DONE 0 but left ending the run to RUN.BAT
            else:
                status = "FAIL"
        elif status == "NO-EXIT":
            status = "PASS" if info["done"] == 0 else "FAIL"
            result["note"] = "unit tester exit did not end 86Box"
        if steps and status == "PASS" and (steps.failed or steps.is_alive()):
            status = "FAIL"                     # the guest ended well, the SSH steps didn't
        if status == "PASS" and a.hazards == "fail" and hazards:
            status = "HAZARD"                   # 86Box saw what goes wrong on a real G400 (patch 0124)
        result["status"] = status
        # Collect guest output files and convert images.
        files = os.path.join(out, "files")
        if d:
            d.get_dir("/OUT", files)
        for fn in sorted(os.listdir(files)) if os.path.isdir(files) else []:
            if fn.upper().endswith(".PPM"):
                try:
                    w, h, rgb = png.read_ppm(os.path.join(files, fn))
                except ValueError:
                    result.setdefault("bad_ppm", []).append(fn)   # (cut short: the guest stopped while writing it)
                    continue
                png.write_png(os.path.join(out, fn[:-4].lower() + ".png"), w, h, rgb)
        vm_nvr = os.path.join(vm, "nvr")
        if status == "PASS" and not a.hdd and not os.path.isdir(nvr_cache) and os.path.isdir(vm_nvr):
            shutil.copytree(vm_nvr, nvr_cache)
        result["f1_taps"] = f1_taps
        shots = os.path.join(vm, "screenshots")
        if os.path.isdir(shots):
            for i, fn in enumerate(sorted(os.listdir(shots))):
                shutil.copyfile(os.path.join(shots, fn), os.path.join(out, "screen-%d%s" % (i, os.path.splitext(fn)[1])))
        if not a.keep_vm:
            shutil.rmtree(vm, ignore_errors=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
        with open(os.path.join(out, "result.json"), "w") as f:
            json.dump(result, f, indent=1)
        with open(os.path.join(out, "status"), "w") as f:
            f.write(result["status"] + "\n")
    print("%-24s %s (%ss)" % (a.name, result["status"], result.get("elapsed_s", "?")))
    return 0 if result["status"] == "PASS" else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", required=True)
    ap.add_argument("--exe", help="DOS program to copy to C:\\TEST and run")
    ap.add_argument("--cmd", action="append", default=[], help="RUN.BAT lines to run instead of --exe")
    ap.add_argument("--game", help="game key from the games file (attached as D:)")
    ap.add_argument("--games-file", default=os.path.join(ROOT, "tools/games/games.json"),
                    help="games list (default tools/games/games.json; DOS-GL has its own)")
    ap.add_argument("--ovl-dst", help="DOS path for --ovl (default C:\\TEST\\GLIDE2X.OVL, or the game's)")
    ap.add_argument("--args", default="")
    ap.add_argument("--hazards", choices=("report", "fail"), default=os.environ.get("LOOPA_HAZARDS", "report"),
                    help="86Box's silicon checks (local patch 0124): always in result.json mga_hazards; with fail, "
                         "a job that would pass gets status HAZARD (default $LOOPA_HAZARDS or report)")
    ap.add_argument("--ovl", help="GLIDE2X.OVL to install as C:\\TEST\\GLIDE2X.OVL")
    ap.add_argument("--file", action="append", default=[], help="SRC[=/DOS/PATH] extra files")
    ap.add_argument("--hdd", help="boot this raw hard disk image as C: instead (in place, written to; no floppy, "
                    "nothing put on it): a bench PC's own disk. Outside the repository and the cache, mount its "
                    "directory into the dev container with MGA_EXTRA_DIR")
    ap.add_argument("--floppy", metavar="IMG", help="with --hdd: this 1.44 MB floppy image in A: (a copy), which the "
                    "BIOS boots before the disk (an installer's floppy and a blank disk)")
    ap.add_argument("--scratch-hdd", metavar="MB", help="a blank disk of MB megabytes (sparse, made for the run) on "
                    "IDE 1:0, unpartitioned, so DOS leaves it alone: for raw-sector tests (GLOS's DSKTORT)")
    ap.add_argument("--pre", action="append", default=[], help="extra RUN.BAT lines before the test")
    ap.add_argument("--keys", default="", help="comma list of SECONDS:SCANCODE[:down|up] after HX-BOOT, "
                    "or SECONDS:mouse:DX:DY[:BUTTONS] (mickeys; buttons bit 0 left, 1 right, 2 middle; needs --mouse), "
                    "or SECONDS:joy:axis|button:N:VALUE (see --joystick); "
                    "an @TEXT item makes the SECONDS after it count from when TEXT appears on the serial line")
    ap.add_argument("--joystick", default="none", help="86Box joystick type (e.g. 2axis_4button, 4axis_4button; "
                    "default 4axis_4button when --keys has joy items), driven by the monitor's joy command "
                    "(local patch 0105) through SECONDS:joy:axis:N:VALUE (-32767..32767) and SECONDS:joy:button:N:0|1")
    ap.add_argument("--wav", action="store_true", help="record the sound card's output to OUT/audio.wav "
                    "(OpenAL Soft's wave backend; use with --sound)")
    ap.add_argument("--mouse", default="none", help="86Box mouse (none, ps2, msserial); any but none also "
                    "loads CuteMouse (C:\\HX\\CTMOUSE.EXE) before the test")
    ap.add_argument("--shots", default="", help="comma list of SECONDS after HX-BOOT to screenshot")
    ap.add_argument("--voodoo", type=int, default=1)
    ap.add_argument("--voodoo-threads", type=int, default=int(os.environ.get("VOODOO_THREADS", "1")))
    ap.add_argument("--voodoo-recompiler", type=int, default=int(os.environ.get("VOODOO_RECOMPILER", "0")))
    ap.add_argument("--g100-mb", type=int, default=8)
    ap.add_argument("--mem", type=int, default=64, help="the PC's RAM in MB (default 64; the BF6 takes up to 768)")
    ap.add_argument("--card", choices=sorted(CARDS), default=None,
                    help="video card: a Matrox g100/g200/g400/g450 (AGP: BF6 only; default $MGA_CARD or g100), "
                    "g200pci/g400pci/g450pci (PCI, local patch 0116: any profile), or vbe (S3 Trio64V2/DX, VESA 2.0; "
                    "the default on the 486 profiles)")
    ap.add_argument("--machine", choices=sorted(PROFILES), default="bf6",
                    help="bf6 (Pentium II 350, the default), 486dx2 (i486DX2-66, no CR4) or 486dx4 "
                    "(iDX4-100, VME/PVI), both on a Shuttle HOT-433A")
    ap.add_argument("--sound", default=None)
    ap.add_argument("--net", choices=sorted(hostio.NICS), help="network card on SLiRP user networking "
                    "(ne2k is ISA at 300h, IRQ 10)")
    ap.add_argument("--net-fwd", action="append", default=[], metavar="[HOST:]GUEST",
                    help="forward a host TCP port to the guest's port (default 22 when --net is given; "
                    "a free host port is picked if HOST is left out; result.json records it)")
    ap.add_argument("--net-dos", action="store_true", help="put the Crynwr NE2000 packet driver and mTCP "
                    "(DHCP, NC) in C:\\PKTDRV and C:\\MTCP, with PATH and MTCPCFG set")
    ap.add_argument("--com2", action="store_true", help="COM2 on a pty bridged to a TCP port on 127.0.0.1 "
                    "(result.json com2_port; guest output also in OUT/com2.log)")
    ap.add_argument("--tcp-send", action="append", default=[], metavar="ANCHOR|TARGET|TEXT",
                    help="when ANCHOR appears on the serial line (in order), connect to TARGET (com2, or "
                    "net:GUESTPORT), send TEXT and CR LF, and keep the reply in OUT/tcp-N.txt")
    ap.add_argument("--ssh-steps", metavar="FILE", help="SSH steps against the guest's port 22 (GLOS): wait, "
                    "exec, expect rc|out|err, put, get, shot (tools/loopa/hostio.py); results in result.json "
                    "ssh_steps and OUT/ssh.log; a failed step fails the job")
    ap.add_argument("--ssh-key", metavar="FILE", help="the private key for --ssh-steps")
    ap.add_argument("--ssh-user", default="glos", help="the user for --ssh-steps")
    ap.add_argument("--wrap", default="", help="prefix for the program's command line in RUN.BAT "
                    "(e.g. C:\\GLOS\\GLOS.EXE /RUN), for --exe and --game jobs")
    ap.add_argument("--dynarec", type=int, choices=(0, 1), default=1, help="86Box's dynamic recompiler")
    ap.add_argument("--boot-cfg", choices=["default"] + sorted(d[4:] for d in os.listdir(HERE) if d.startswith("dos-")),
                    default="default", help="boot floppy variant: default (FreeDOS 1.4, no XMS driver), himemx "
                    "(HIMEMX.EXE and DOS=HIGH), umbpci (UMBPCI.SYS's UMBs, HIMEMX, DOS=HIGH,UMB), glosshell, "
                    "glosshell-himemx and glosshell-umbpci (GLOS as the shell, from C:\\TEST\\GLOS.EXE); "
                    "tools/loopa/mkgolden-variant.sh")
    ap.add_argument("--timeout", type=float, default=float(os.environ.get("LOOPA_TIMEOUT", 300)))
    ap.add_argument("--idle", type=float, default=float(os.environ.get("LOOPA_IDLE", 60)))
    ap.add_argument("--boot-grace", type=float, default=45)
    ap.add_argument("--out")
    ap.add_argument("--keep-vm", action="store_true")
    ap.add_argument("--emit-config", action="store_true",
                    help="print the generated 86box.cfg, RUN.BAT and golden key, and exit (tools/loopa/cfgcheck.py)")
    # LOOPA_EXTRA_ARGS: more options for every job, whoever starts it (GLOS's
    # survey loads a DPMI host this way into other repos' jobs).
    import shlex
    a = ap.parse_args(sys.argv[1:] + shlex.split(os.environ.get("LOOPA_EXTRA_ARGS", "")))
    if a.ssh_steps and not (a.ssh_key and a.net):
        ap.error("--ssh-steps needs --ssh-key and --net")
    if a.ssh_steps:
        hostio.parse_ssh_steps(a.ssh_steps)     # a bad steps file fails now, not mid-run
    sys.exit(emit_config(a) if a.emit_config else run(a))


if __name__ == "__main__":
    main()
