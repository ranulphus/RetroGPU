# Loop B: the physical bench

Real-silicon loop (PRD §11.7, D7). Nothing here needs a person except
provisioning and resetting a machine after a hard hang when it has no
reset hook.

## How a job runs

1. `tools/bench/run.py` (on the build host, not in the dev container)
   writes the job to `dist/bench/<pc>/job/`, a `FETCH.BAT` and a new
   `LATEST.TXT`. It serves `dist/bench` over HTTP unless
   `tools/bench/serve.sh` already does, and starts an upload-only FTP
   receiver (`tools/bench/ftpsink.py`) on the PC's `ftp_port`.
2. The PC's poller (`C:\HX\BENCH.BAT`) sees `LATEST.TXT` change, fetches
   the job into `C:\TEST` with mTCP `HTGET` (three tries per file) and
   runs `C:\TEST\RUN.BAT`. The job carries its DPMI host: `CWSDPMI.EXE` for
   DJGPP programs (detected from the go32 stub; DOS-GL), `DOS4GW.EXE`
   otherwise (`--extender` overrides).
3. The program reports over COM1 (`HX-` lines, and `MGL-` lines from the
   runtime). A snapshot with no 86Box unit tester prints
   `HX-CAPTURE <name>` and holds the frame for `HX_CAPWAIT` seconds; the
   host grabs it from the PC's capture device.
4. `RUN.BAT` uploads `C:\OUT` with mTCP `FTP` (a generated script of `put`
   lines; the password is a per-job token), then the poller reboots the PC.
   `REBOOT.COM` resets through the PCI reset control register (port
   `CF9h`, a full chipset reset) and falls back to the keyboard controller;
   a keyboard-controller reset alone resets only the CPU and in 86Box
   sometimes left the machine executing garbage at the reset vector.
5. Results land in `out/bench/<pc>/<job>/`: `serial.log`, `files/` (the
   PC's `C:\OUT`), PNGs converted from its PPMs, `capture-*.png`,
   `result.json`. On `HANG` or `TIMEOUT` the PC's `reset` hook runs.

Statuses: `PASS FAIL CRASH HANG TIMEOUT NOT-PICKED-UP`.

## Through GLOS, over SSH

A bench PC that runs GLOS (the graphical DOS; its agent is an SSH server in the supervisor) is driven
through it instead, with the poller as the fallback (GLOS PRD D10, H8):

- **On the PC,** AUTOEXEC starts `C:\GLOS\GLOS.EXE` before the packet driver and `BENCH.BAT`. GLOS owns the
  network card while it runs, so the two never run together. If GLOS can't start, or is told to leave
  (`glos exit`), the boot goes on to the poller.
- **On the host,** the PC's entry in `bench.toml` has `ssh = "HOST[:PORT]"` (and `ssh_key`). `run.py` asks
  there for `glos ver`; if GLOS answers, the job goes over SSH, and otherwise through the poller as
  above. `--transport ssh` or `poller` insists on one.
- **A job over SSH:**
  1. the same files and `RUN.BAT`, without its FTP upload, go to `C:\TEST` by SFTP;
  2. `RUN.BAT` runs as a job of GLOS's agent;
  3. the PC's COM1 lines come back through `glos log --follow`, so the `HX-` protocol is unchanged and no
     serial cable is needed;
  4. `C:\OUT` is fetched by SFTP into `files/`; what the job printed goes to `stdout.txt`;
  5. the PC does not reboot. On `HANG` or `TIMEOUT` the job is killed (`glos kill`), and the `reset` hook
     runs only if even that gets no answer.
- `result.json` says which way the job went (`"transport"`).
- `tools/bench/glosssh.py HOST[:PORT] KEY COMMAND...` runs one command on such a PC (`glos shot > s.png`,
  `glos stat`, a DOS command line).

## Machines

`tools/bench/bench.toml` lists the PCs; machine-specific values go in
`tools/bench/bench.local.toml` (not committed, same layout, overrides per
key). Each PC has a card, a serial device on the build host, an FTP port,
an optional V4L2 capture device (and ffmpeg input options), an optional
reset command, and the directory its games are installed in.

`drive` (default `C`) is the drive holding a job's `TEST` and `OUT` folders. Set it where C: isn't
ours: on bench G, GLOS runs natively beside the owner's Windows 98, which is C: and must never be
written, so its entry says `drive = "D"`. run.py then writes `D:\TEST` and `D:\OUT`, rewrites
`C:\TEST`, `C:\OUT` and a bare `C:` in RUN.BAT (callers' `--cmd` lines included), and refuses a job
in which any other `C:` remains. Such a PC runs over ssh only; the poller's jobs use `C:\HX`.

| PC | Card | Serial port (default) | Notes |
|---|---|---|---|
| bench-g100 | Productiva G100 | /dev/ttyS0 | milestone S |
| bench-g200 | G200 | /dev/ttyUSB0 | M5 |
| bench-g400 | G400 AGP 16 MB | /dev/ttyUSB1 | IBM PC 300PL (6862-270): Pentium II 448 MB, 440BX, onboard PRO/100 (IRQ 11, shared with USB). Windows 98 SE and Ubuntu 16.04 (`ssh ibm300pl`); GLOS from the DOS menu's GLOS item in agent mode (`ssh bench-g400`; `sudo glos-boot` from Ubuntu boots it once). Set up 2026-10-26; no serial cable yet |
| bench-g450 | G450 PCI 32 MB | /dev/ttyUSB2 | M6; DVI output for the capture device |
| vbench-g200, vbench-g100 | 86Box | `file:out/vbench/<card>/serial.log` | the dry run below |

The build host has one real UART (`ttyS0`); the others need USB serial
adapters. The user running `run.py` needs to be in the `dialout` group
(serial) and the `video` group (capture).

## Provisioning checklist (per PC)

1. MS-DOS 7.1 (FAT32) or 6.22 on C:, `HIMEM.SYS` only (no EMM386).
2. NIC with a DOS packet driver in `C:\PKTDRV\`; mTCP (`DHCP`, `HTGET`,
   `FTP`, pinned in `tools/setup/versions.mk`) in `C:\MTCP\` with
   `TCP.CFG` (`PACKETINT 0x60`) and `SET MTCPCFG=C:\MTCP\TCP.CFG`.
3. Null-modem cable from COM1 to the build host's serial port above.
4. Video capture: the card's output into the capture device (the Epiphan
   DVI2USB 3.0 is a UVC device: `capture = "/dev/videoN"`). Check it with
   `tools/bench/capture.py list`, `formats /dev/videoN` and
   `grab /dev/videoN x.png`, which reports the mode it saw (720x400 for
   DOS text). Epiphan's own tool may be needed once to switch the unit's
   firmware to UVC mode.
5. Recommended: a USB relay or smart plug on reset or power, with the
   command that pulses it as `reset`.
6. Copy `tools/bench/dos/BENCH.BAT` and `build/ow/dos/*.COM` to `C:\HX\`.
   At the end of `AUTOEXEC.BAT`: load the packet driver,
   `SET HX_URL=http://<host>:8000/<pc>` and `CALL C:\HX\BENCH.BAT`.
7. Install the games once: `C:\GAMES\GTA\` and `C:\GAMES\SR\` from the
   same archives as Loop A. Game jobs back up the retail `GLIDE2X.OVL` as
   `GLIDE2X.MGB` once, swap in the job's OVL and restore it afterwards.
8. Boot once; the harness should show `HX-BOOT` and `HX-IDLE` lines:
   `python3 tools/bench/serlog.py --port /dev/ttyS0`.

## Running

```
tools/bench/run.py --pc bench-g200 --exe build/ow/dos/PROBE.EXE
tools/bench/run.py --pc bench-g200 --exe build/ow/dos/CONFORM.EXE --args t04 \
    --ovl build/ow/GLIDE2X.OVL
tools/bench/run.py --pc bench-g200 --replay ~/.cache/mga-glide/traces/gta-voodoo-300.bin \
    --frames 60,150 --ovl build/ow/GLIDE2X.OVL
tools/bench/run.py --pc bench-g200 --game sr --ovl build/ow/GLIDE2X.OVL \
    --set MGAGLIDE=exit_after=400
tools/bench/run.py --pc bench-g450 --file A.EXE --file B.EXE --ovl build/ow/GLIDE2X.OVL \
    --cmd "A.EXE --x" --cmd "B.EXE --glide=C:\TEST\GLIDE2X.OVL"
```

bench-g400's DOS refuses an `=` in a `SET` value ("Syntax error", nothing set), so `run.py` writes
`--set MGAGLIDE=...` into `C:\TEST\MGAGLIDE.CFG` instead, which Glide reads from the current directory. With
`--game` (another directory) it stays a `SET` line, and `run.py` warns.

`--cmd` runs several programs in one job, each line in turn in `C:\TEST`
(DOSBench runs its OpenGL and Glide programs back to back this way): every
EXE shipped with `--file` gets its DOS extender, and the job passes when every
program reports `HX-DONE 0`. Loop A's `run.py --cmd` likewise waits for every
program that reports `HX-START` to report `HX-DONE` (and, for DOS-GL programs
run with `DGL_EXIT_AFTER`, every `DGL-START` to be matched by a `DGL-EXIT`).

Game jobs need a game that runs unattended: Screamer Rally's attract-mode
race does; GTA waits at its menus for Enter, which only Loop A can press.
The runtime logs nothing per frame, so game jobs rely on `--timeout`
rather than the idle rule.

## Dry run in 86Box

`tools/bench/vpc.py` boots an emulated PC provisioned like a bench PC
(NE2000 on SLiRP with the Crynwr packet driver, mTCP, `BENCH.BAT`, COM1
to a log file, no unit tester) and leaves it polling:

```
MGA_DOCKER_NETWORK=host tools/dev python3 tools/bench/vpc.py start g200 [--game sr]
tools/bench/run.py --pc vbench-g200 --exe build/ow/dos/PROBE.EXE
tools/bench/vpc.py screenshot g200        # out/vbench/g200/vm/screenshots/
tools/bench/vpc.py stop g200
```

The container needs the host's network so SLiRP's `10.0.2.2` reaches
`run.py`'s HTTP and FTP ports. The dry run covers pick-up, fetch, the
`HX-CAPTURE` path, FTP upload, reboot, hang detection and the reset hook;
frames from it match Loop A pixel for pixel.

With GLOS: `vpc.py start g200 --glos DIR`, where DIR holds `GLOS.EXE`, `GLOSK.BIN`, `keys/HOSTKEY` and
`keys/AUTHKEYS` (GLOS's `make glos-cache` fills `~/.cache/mga-glide/glos/bin` so, with its public test
keys). GLOS's SSH port is forwarded to the host's loopback at the port `bench.toml` gives the PC.

Dry run of the SSH transport (2026-10-11, vbench-g200, GLOS at its M5 A8):

| Job | Way | Result |
|---|---|---|
| `--exe PROBE.EXE` | ssh (auto) | PASS, 17 s from start to result |
| `--exe HELLO.EXE` | ssh (auto) | PASS |
| after `glos exit`: `--exe PROBE.EXE` | poller (auto: nothing answered) | PASS; the PC rebooted into GLOS again |
| `--cmd` lines writing a text file and a 265 KB file into `C:\OUT` | ssh | PASS, both fetched, the large one intact |
| `--cmd "WAITSEC 600" --timeout 25` | ssh | TIMEOUT, killed by `glos kill`, no reset; the next job PASS |
