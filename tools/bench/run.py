#!/usr/bin/env python3
"""Loop B push-and-run: publish a job for a bench PC and collect results.

Runs on the build host itself (not in the dev container: the PCs must reach
its HTTP and FTP ports). Two ways to the PC:

  ssh     GLOS is up on the PC (its SSH server answers at the PC's `ssh`
          address in bench.toml): the job's files go to C:\\TEST by SFTP,
          RUN.BAT runs as a job of GLOS's agent, the PC's COM1 lines come
          through `glos log --follow`, C:\\OUT comes back by SFTP. The PC
          doesn't reboot; a hung job is killed (`glos kill`).
  poller  otherwise, as always: the PC's BENCH.BAT poller fetches
          dist/bench/<pc>/FETCH.BAT when LATEST.TXT changes, runs
          TEST\\RUN.BAT, uploads C:\\OUT to this script's FTP sink and reboots.

--transport picks one; the default, auto, asks the PC for `glos ver` and
falls back to the poller when nothing answers.

  run.py --pc bench-g200 --exe build/ow/dos/PROBE.EXE
  run.py --pc bench-g200 --exe build/ow/dos/CONFORM.EXE --ovl build/ow/GLIDE2X.OVL --args "t04"
  run.py --pc bench-g200 --replay TRACE.BIN --frames 60,150 --ovl build/ow/GLIDE2X.OVL
  run.py --pc bench-g200 --game gta --ovl build/ow/GLIDE2X.OVL --set MGAGLIDE_EXIT=600
  run.py --pc bench-g450 --file A.EXE --file B.EXE --cmd "A.EXE --x" --cmd "B.EXE --y"

--cmd runs several programs in one job (DOSBench runs its Glide and OpenGL
programs back to back): each line goes into RUN.BAT in turn, every EXE
shipped with --file gets its DOS extender, and the job passes when every
program reports HX-DONE 0.

PCs and their serial ports, SSH addresses, capture devices and reset hooks
come from tools/bench/bench.toml and bench.local.toml. A PC's `drive` (default
C) holds TEST and OUT: where C: isn't ours (bench G, GLOS native beside the
owner's Windows 98 on C:), drive = "D" moves them, and a job naming C:
anywhere else is refused. Results:
out/bench/<pc>/<job>/ (serial.log, files/ = the PC's C:\\OUT, *.png,
capture-*.png, result.json; over ssh also stdout.txt, what the job printed).

Statuses: PASS FAIL CRASH HANG TIMEOUT NOT-PICKED-UP.
"""
import argparse
import atexit
import functools
import http.server
import json
import os
import re
import secrets
import shutil
import socket
import subprocess
import sys
import threading
import time
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "loopa"))
import capture  # noqa: E402
import ftpsink  # noqa: E402
import glosssh  # noqa: E402
import png  # noqa: E402
import serlog  # noqa: E402

WATCOM = os.environ.get("WATCOM", os.path.expanduser("~/.local/opt/watcom-20260901"))
DJGPP_PREFIX = os.environ.get("DJGPP_PREFIX", os.path.expanduser("~/.local/opt/djgpp-gcc1220"))
EXTENDERS = {"dos4gw": (os.path.join(WATCOM, "binw/dos4gw.exe"), "DOS4GW.EXE"),
             "cwsdpmi": (os.path.join(DJGPP_PREFIX, "dos/CWSDPMI.EXE"), "CWSDPMI.EXE")}


def extender_of(exe):
    """DJGPP programs carry the go32 stub and need CWSDPMI; the rest DOS/4GW."""
    with open(exe, "rb") as f:
        head = f.read(4096)
    return "cwsdpmi" if b"go32stub" in head or b"CWSDPMI" in head else "dos4gw"
DOS83 = re.compile(r"^[A-Z0-9_$~!#%&-]{1,8}(\.[A-Z0-9_$~!#%&-]{1,3})?$")


def load_config():
    cfg = tomllib.load(open(os.path.join(HERE, "bench.toml"), "rb"))
    local = os.path.join(HERE, "bench.local.toml")
    if os.path.exists(local):
        over = tomllib.load(open(local, "rb"))
        cfg["host"].update(over.get("host", {}))
        for pc, vals in over.get("pc", {}).items():
            cfg["pc"].setdefault(pc, {}).update(vals)
    return cfg


def ensure_http(port, root):
    """Serve dist/bench for the job unless tools/bench/serve.sh already is."""
    try:
        socket.create_connection(("127.0.0.1", port), timeout=1).close()
        return None
    except OSError:
        pass
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *a):
            pass
    handler = functools.partial(Quiet, directory=root)
    srv = http.server.ThreadingHTTPServer(("0.0.0.0", port), handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def dos_lines(lines):
    return ("\r\n".join(["@ECHO OFF"] + lines) + "\r\n").encode("ascii")


class Capture:
    """Grab single frames from the PC's capture device on HX-CAPTURE."""

    def __init__(self, dev, opts, out):
        self.dev, self.opts, self.out, self.threads = dev, opts, out, []

    def grab(self, name):
        if not self.dev:
            return
        dst = os.path.join(self.out, "capture-%s.png" % re.sub(r"[^\w.-]", "_", name))
        cmd = capture.grab_cmd(self.dev, dst, self.opts)
        t = threading.Thread(target=subprocess.call, args=(cmd,), daemon=True)
        t.start()
        self.threads.append(t)

    def wait(self):
        for t in self.threads:
            t.join(30)


def with_sersay(pc, run):
    """RUN.BAT's lines, SERSAY called from C:\\TEST on a PC whose bench.local.toml says sersay = "test" (one with no
    C:\\HX of the harness's tools, as the P3, where only C:\\GLOS and C:\\TEST are ours): the job carries it."""
    if pc.get("sersay") != "test":
        return run
    return [("C:\\TEST\\SERSAY.COM" + l[6:]) if l.startswith("SERSAY ") else l for l in run]


DRIVE_PATH = re.compile(r"\bC:\\(TEST|OUT)\b", re.I)


def drive_of(pc):
    """The PC's drive for TEST and OUT: bench.toml's `drive`, C unless C: isn't ours there (bench G: since GLOS runs
    natively, C: is the owner's Windows 98 partition, never to be written, and ours are D:\\TEST and D:\\OUT;
    ROSS, 2026-11-27)."""
    d = str(pc.get("drive", "C")).upper().rstrip(":")
    if not re.fullmatch(r"[C-Z]", d):
        sys.exit("bench: drive = %r in bench.toml is not a drive letter" % pc.get("drive"))
    return d


def on_drive(pc, run):
    """RUN.BAT's lines with C:\\TEST, C:\\OUT and the bare "C:" moved to the PC's drive (callers' --cmd lines too, so
    their scripts run unchanged); on a PC whose drive isn't C, a line that still names C: stops the job."""
    d = drive_of(pc)
    if d == "C":
        return run
    out = []
    for line in run:
        line = DRIVE_PATH.sub(lambda m: "%s:\\%s" % (d, m.group(1).upper()), line)
        if line.strip().upper() == "C:":
            line = d + ":"
        if re.search(r"(?i)(^|[^A-Z0-9])C:", line):
            sys.exit("bench: this PC's drive is %s: and nothing on C: is ours; RUN.BAT would name C: in: %s" % (d, line))
        out.append(line)
    return out


def build_job(a, pc, cfg, job, token, jd, upload=True):
    """Write the job directory, RUN.BAT and (for the poller, which uploads by FTP) UP.TXT; return the file
    names."""
    if upload and drive_of(pc) != "C":
        sys.exit("bench: the poller's jobs use C:\\HX and C:\\TEST; a PC with drive = \"%s\" runs over ssh only"
                 % drive_of(pc))
    shutil.rmtree(jd, ignore_errors=True)
    os.makedirs(jd)
    files = []

    def add(src, name=None):
        name = (name or os.path.basename(src)).upper()
        if not DOS83.match(name):
            sys.exit("bench: %s is not a DOS 8.3 name" % name)
        shutil.copyfile(src, os.path.join(jd, name))
        files.append(name)
        return name

    exts = [a.extender] if a.extender else []
    if not exts:
        exes = ([a.exe] if a.exe else []) + [f.partition("=")[0] for f in a.file
                                             if (f.partition("=")[2] or f).upper().endswith(".EXE")]
        exts = sorted({extender_of(e) for e in exes}) or ["dos4gw"]
    for ext in exts:
        add(*EXTENDERS[ext])
    if a.ovl:
        add(a.ovl, "GLIDE2X.OVL")
    for spec in a.file:
        src, _, name = spec.partition("=")
        add(src, name or None)
    if pc.get("sersay") == "test":
        add(os.path.join(ROOT, "build/ow/dos/SERSAY.COM"))
    host = cfg["host"]
    addr = pc.get("address", host["address"])
    run = ["C:", "CD \\TEST", "IF NOT EXIST C:\\OUT\\NUL MD C:\\OUT",
           "ECHO Y| DEL C:\\OUT\\*.* > NUL", "SET HX_CAPWAIT=%d" % a.capwait]
    if drive_of(pc) != "C":
        # (the hx shim's frames and files: C:\OUT unless HX_OUT says otherwise; programs' own paths are theirs)
        run += ["SET HX_OUT=%s:\\OUT" % drive_of(pc)]
    for spec in a.set:
        name, _, value = spec.partition("=")
        if "=" not in value:
            run += ["SET %s" % spec]
        elif name.upper() == "MGAGLIDE" and not a.game:
            # DOS's SET (bench-g400's) answers "Syntax error" to an "=" in a value and sets nothing:
            # Glide's options go in C:\TEST\MGAGLIDE.CFG instead (read from the current directory).
            open(os.path.join(jd, "MGAGLIDE.CFG"), "wb").write(("\r\n".join(value.split()) + "\r\n").encode())
            files.append("MGAGLIDE.CFG")
        else:
            run += ["SET %s" % spec]
            print("bench: warning: some DOSes (bench-g400's) refuse '=' in a SET value (--set %s)" % spec,
                  file=sys.stderr)
    run += ["SERSAY HX-JOB %s" % job]
    if a.game:
        game = json.load(open(os.path.join(ROOT, "tools/games/games.json")))[a.game]
        gdir = pc["games"].rstrip("\\") + "\\" + game["cwd"]
        ovl = pc["games"].rstrip("\\") + "\\" + game["ovl"]
        run += [gdir[:2], "CD %s" % gdir[2:],
                "IF NOT EXIST %s.MGB COPY %s %s.MGB > NUL" % (ovl[:-4], ovl, ovl[:-4])]
        if a.ovl:
            run += ["COPY C:\\TEST\\GLIDE2X.OVL %s > NUL" % ovl]
        run += [l + (" " + a.args if a.args else "") for l in game["run"]]
        run += ["COPY %s.MGB %s > NUL" % (ovl[:-4], ovl), "C:", "CD \\TEST"]
    elif a.cmd:
        run += a.cmd
    else:
        run += ["%s %s" % (add(a.exe), a.args)]
    if not upload:                              # (over ssh the host fetches C:\OUT itself)
        run += ["SERSAY HX-EXIT", "SERSAY HX-JOB-END"]
        open(os.path.join(jd, "RUN.BAT"), "wb").write(dos_lines(on_drive(pc, with_sersay(pc, run))))
        return files + ["RUN.BAT"]
    run += ["SERSAY HX-EXIT",
            "COPY C:\\TEST\\UP.TXT C:\\HX\\UP.TXT > NUL",
            "FOR %%F IN (C:\\OUT\\*.*) DO ECHO put %%F>> C:\\HX\\UP.TXT",
            "ECHO quit>> C:\\HX\\UP.TXT",
            "SERSAY HX-UPLOAD",
            "FTP -port %d %s < C:\\HX\\UP.TXT > C:\\HX\\FTP.LOG" % (pc["ftp_port"], addr),
            "SERSAY HX-UPLOAD-END"]
    open(os.path.join(jd, "RUN.BAT"), "wb").write(dos_lines(with_sersay(pc, run)))
    open(os.path.join(jd, "UP.TXT"), "wb").write(("bench\r\n%s\r\n" % token).encode())
    return files + ["RUN.BAT", "UP.TXT"]


def main():
    cfg = load_config()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pc", required=True, choices=sorted(cfg["pc"]))
    ap.add_argument("--name")
    ap.add_argument("--exe")
    ap.add_argument("--args", default="")
    ap.add_argument("--ovl")
    ap.add_argument("--file", action="append", default=[], help="SRC[=NAME.EXT] extra file for C:\\TEST")
    ap.add_argument("--set", action="append", default=[], help="VAR=VALUE environment for the job")
    ap.add_argument("--replay", help="GLPLAY a call trace (tools/gltrace) with --ovl")
    ap.add_argument("--frames", default="", help="frames GLPLAY captures (with --replay)")
    ap.add_argument("--game", help="game key from tools/games/games.json, installed on the PC")
    ap.add_argument("--cmd", action="append", default=[], help="RUN.BAT line (repeatable) instead of --exe")
    ap.add_argument("--direct", action="store_true",
                    help="ssh: the job's session in GLOS's direct mode (glos run --direct: IOPL 3, the real interrupt flag)")
    ap.add_argument("--timeout", type=float, default=900, help="seconds from pick-up to HX-EXIT")
    ap.add_argument("--pickup", type=float, default=300, help="seconds for the PC to pick the job up")
    ap.add_argument("--idle", type=float, help="silent seconds that count as a hang (default 180; games "
                    "log nothing per frame, so for --game only --timeout applies)")
    ap.add_argument("--capwait", type=int, default=4, help="seconds the PC holds a frame on HX-CAPTURE")
    ap.add_argument("--transport", choices=["auto", "ssh", "poller"], default="auto",
                    help="ssh: through GLOS's agent on the PC; poller: its BENCH.BAT; auto (default): ssh if "
                    "GLOS answers at the PC's ssh address, else the poller")
    ap.add_argument("--extender", choices=sorted(EXTENDERS),
                    help="DPMI host shipped with the job (default: CWSDPMI for DJGPP programs, else DOS/4GW)")
    a = ap.parse_args()
    if a.replay:
        a.exe = a.exe or os.path.join(ROOT, "build/ow/dos/GLPLAY.EXE")
        a.file.append("%s=TRACE.BIN" % a.replay)
        a.args = ("C:\\TEST\\TRACE.BIN %s %s" % (a.frames, a.args)).strip()
    if a.idle is None:
        a.idle = a.timeout if a.game else 180
    if not (a.exe or a.game or a.cmd):
        ap.error("one of --exe, --replay, --game, --cmd is needed")
    pc = cfg["pc"][a.pc]
    host = cfg["host"]
    job = "%s-%d" % (a.name or a.game or (os.path.basename(a.exe).split(".")[0].lower() if a.exe else "cmd"),
                     int(time.time()))
    token = secrets.token_hex(6)
    pub = os.path.join(ROOT, "dist/bench", a.pc)
    out = os.path.join(ROOT, "out/bench", a.pc, job)
    os.makedirs(out, exist_ok=True)
    glos = None
    if a.transport != "poller" and pc.get("ssh"):
        glos = glosssh.Glos(pc["ssh"], pc.get("ssh_key") or os.environ.get("GLOS_KEY", "~/GLOS/tests/keys/client"))
        # (its connection closed however this ends: a job that failed here before reaching the PC, its file names
        # say, left its ControlMaster for ControlPersist's hour, and four such held the P3's SSH, which refused every
        # other connection, 2026-11-26 and 11-27; close() twice is harmless)
        atexit.register(glos.close)
        if not glos.up():
            glos.close()
            glos = None
    if a.transport == "ssh" and not glos:
        sys.exit("bench: GLOS doesn't answer at %s (bench.toml: pc.%s.ssh)" % (pc.get("ssh") or "no address", a.pc))
    http_srv = sink = runner = direct_job = None
    if glos:
        jd = os.path.join(pub, "job")
        names = build_job(a, pc, cfg, job, token, jd, upload=False)
        drive = drive_of(pc)
        rc, said = glos.put([(os.path.join(jd, n), n) for n in names], "/%s/TEST" % drive)
        if rc != 0:
            sys.exit("bench: sftp to %s failed: %s" % (pc["ssh"], said.strip()[-200:]))
        lines = glos.follow()
        if a.direct:
            # (glos run --direct posts the job and may return at once: the job's end from its HX lines alone)
            direct_job = glos.start("glos run --direct %s:\\TEST\\RUN.BAT" % drive)
        else:
            runner = glos.start("%s:\\TEST\\RUN.BAT" % drive)
        print("bench: started %s on %s (%s) over ssh (%s)" % (job, a.pc, pc["card"], pc["ssh"]), flush=True)
    else:
        names = build_job(a, pc, cfg, job, token, os.path.join(pub, "job"))
        fetch = ["IF NOT EXIST C:\\TEST\\NUL MD C:\\TEST", "ECHO Y| DEL C:\\TEST\\*.* > NUL"]
        # HTGET exits 20-29 on a 2xx reply. Each file gets three tries: a fetch
        # straight after another one occasionally fails at the socket level.
        for i, n in enumerate(names):
            fetch += ["SET HX_TRY=",
                      ":F%d" % i,
                      "SET HX_TRY=%HX_TRY%x",
                      "HTGET -o C:\\TEST\\%s %%HX_URL%%/job/%s > NUL" % (n, n),
                      "IF ERRORLEVEL 30 GOTO R%d" % i,
                      "IF ERRORLEVEL 20 GOTO K%d" % i,
                      ":R%d" % i,
                      "IF NOT \"%%HX_TRY%%\"==\"xxx\" GOTO F%d" % i,
                      "SERSAY HX-FETCH-FAIL %s" % n,
                      ":K%d" % i]
        open(os.path.join(pub, "FETCH.BAT"), "wb").write(dos_lines(fetch))

        http_srv = ensure_http(host["http_port"], os.path.join(ROOT, "dist/bench"))
        # PASV replies name the address the PC used; behind SLiRP that is the
        # PC's own view of the host (pc.address), not the socket's local one.
        sink = ftpsink.FtpSink(os.path.join(out, "files"), pc["ftp_port"], pc.get("address"),
                               "bench", token).start()
        lines = serlog.Lines(pc["serial"], os.path.join(ROOT, "out/bench", a.pc, "serial.log"))
        open(os.path.join(pub, "LATEST.TXT"), "w").write(job + "\n")
        print("bench: published %s for %s (%s); waiting on %s" % (job, a.pc, pc["card"], pc["serial"]), flush=True)
    cap = Capture(pc.get("capture", ""), pc.get("capture_opts", ""), out)
    log = open(os.path.join(out, "serial.log"), "w")
    t0 = time.time()
    picked = exited = upload_end = done = ended = None
    last, status, game_exit = t0, None, False
    while True:
        now = time.time()
        got = lines.poll()
        for text in got:
            last = now
            if text.startswith("HX-FETCH-FAIL"):
                print(text, flush=True)
            if picked is None:
                if text.startswith("HX-JOB ") and text.split()[1:2] == [job]:
                    picked = now
                    print("bench: picked up", flush=True)
                continue
            log.write(text + "\n")
            log.flush()
            print(text, flush=True)
            if text.startswith("HX-CAPTURE "):
                cap.grab(text.split(None, 1)[1])
            elif text.startswith("HX-DONE "):
                # Every program must pass (--cmd jobs run several).
                done = (done is not False) and text.split()[1:2] == ["0"]
            elif text.startswith("MGL-EXIT frames="):
                game_exit = True
            elif text.startswith("HX-EXIT"):
                exited = now
            elif text.startswith("HX-UPLOAD-END"):
                upload_end = now
            elif text.startswith("HX-JOB-END") or text.startswith("HX-BOOT"):
                upload_end = upload_end or now
        if upload_end or (exited and now - exited > 300):
            break
        if runner and runner.poll() is not None:        # (the agent's job has ended: its last lines, then out)
            ended = ended or now
            if now - ended > 5:
                break
        if picked is None and now - t0 > a.pickup:
            status = "NOT-PICKED-UP"
            break
        if picked and not exited and now - picked > a.timeout:
            status = "TIMEOUT"
            break
        if picked and not exited and now - last > a.idle:
            status = "HANG"
            break
        if not got:
            time.sleep(0.05)
    if status is None:
        if a.game:
            status = "PASS" if game_exit else "CRASH"
        else:
            status = {True: "PASS", False: "FAIL", None: "CRASH"}[done]
    stuck = status in ("HANG", "TIMEOUT")
    files = os.path.join(out, "files")
    if glos:
        if stuck:                               # GLOS is still there beside a hung program: it kills it
            print("bench: glos kill", flush=True)
            glos.ssh("glos kill")
        proc = runner or direct_job
        try:
            said = proc.communicate(timeout=60)[0]
            stuck = False
        except subprocess.TimeoutExpired:       # (not even that: the PC needs its reset)
            proc.kill()
            said = proc.communicate()[0]
        open(os.path.join(out, "stdout.txt"), "wb").write(said)
        # (after a hang the PC may have gone down at the kill: fetching from a dead host waited out sftp's whole
        # time-out, and no result was written; Echoes, 2026-11-25)
        received = [] if stuck or (status in ("HANG", "TIMEOUT") and not glos.up()) else glos.get_dir("/%s/OUT" % drive_of(pc), files)
        upload_end = upload_end or (not stuck and time.time())
        glos.close()
    if stuck and pc.get("reset"):
        print("bench: resetting %s: %s" % (a.pc, pc["reset"]), flush=True)
        subprocess.call(pc["reset"], shell=True, cwd=ROOT)
    cap.wait()
    if sink:
        sink.stop()
        received = sink.received
    if http_srv:
        http_srv.shutdown()
    os.makedirs(files, exist_ok=True)
    for fn in sorted(os.listdir(files)):
        if fn.upper().endswith(".PPM"):
            w, h, rgb = png.read_ppm(os.path.join(files, fn))
            png.write_png(os.path.join(out, fn[:-4].lower() + ".png"), w, h, rgb)
    result = {"job": job, "pc": a.pc, "card": pc["card"], "status": status, "transport": "ssh" if glos else "poller",
              "exe": a.exe, "args": a.args, "ovl": a.ovl, "game": a.game, "cmd": a.cmd,
              "uploaded": received, "upload_complete": bool(upload_end),
              "elapsed_s": round(time.time() - t0, 1)}
    json.dump(result, open(os.path.join(out, "result.json"), "w"), indent=1)
    print("bench: %s %s (%d files %s) -> %s" % (job, status, len(received), "fetched" if glos else "uploaded", out),
          flush=True)
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
