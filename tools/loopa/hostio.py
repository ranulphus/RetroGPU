"""Host-side I/O for Loop A jobs: the network card's 86Box config, the COM2
bridge (a pty that 86Box opens, relayed to a TCP port), and --tcp-send
steps that talk to the guest through either of them. Used by run.py."""
import os
import select
import socket
import threading
import time
import tty

# 86Box network cards run.py offers (--net), with any config section they need.
# NE2000 (ISA) sits where the Crynwr packet driver looks for it (--net-dos).
NICS = {
    "ne2k": "[NE2000 Compatible #1]\nbase = 0300\nirq = 10\n",
    "ne2kpci": "",              # Realtek RTL8029AS
    "rtl8139c+": "",            # Realtek RTL8139C+
    "i82557": "",               # Intel PRO/100
    "i82558": "",               # Intel PRO/100+
    "pcnetpci": "",             # AMD PCnet-PCI II (Am79C970A): GLOS's kernel/drv/pcnet.c (phase H)
    "dec_21140_tulip_migrate": "",  # DEC 21140 (Tulip; 86Box's card of that name, its default variant the 21140):
                                    # GLOS's kernel/drv/tulip.c (phase H)
    "dec_21143_tulip": "",      # DEC 21143 (Tulip)
    # 86Box's PLIP device on LPT1 (378h, IRQ 7): Ethernet frames over the parallel port as Crynwr's PLIP and
    # Linux's plip speak them, joined to SLiRP like a card. No card in the PC: the guest's own PLIP driver is
    # the network (GLOS: [net] cable = client LPT1).
    "plip": "[Parallel Line Internet Protocol #1]\nport = 0\n",
}


def free_port():
    """A TCP port nothing is listening on now (SLiRP binds 0.0.0.0)."""
    s = socket.socket()
    s.bind(("0.0.0.0", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def parse_forwards(specs):
    """--net-fwd [HOST:]GUEST items -> [(host or None, guest)]."""
    out = []
    for spec in specs:
        host, _, guest = spec.rpartition(":")
        out.append((int(host) if host else None, int(guest)))
    return out


def net_config(card, forwards, switch=None, second=None):
    """[Network] and SLiRP port forwarding sections; forwards [(host, guest)],
    host may be a placeholder string (run.py --emit-config).

    switch: the card is on 86Box's local network switch of that name instead
    of SLiRP: the 86Boxes on this computer whose cards name the same switch
    are one Ethernet (no DHCP server, no forwards; the name is the switch's
    "secret", which keeps runs apart). second: (card, name), a second card on
    such a switch beside the first on SLiRP: a PC on two networks. A PLIP
    device on a switch takes every frame (promiscuous), as a cable does: the
    switch never learns the address the guest gives its end."""
    text = "\n[Network]\nnet_01_card = %s\nnet_01_net_type = %s\n" % (card, "nlswitch" if switch else "slirp")
    if switch:
        text += "net_01_secret = %s\n" % switch
        if card == "plip":
            text += "net_01_promisc = 1\n"
        forwards = []
    if second:
        text += "net_02_card = %s\nnet_02_net_type = nlswitch\nnet_02_secret = %s\n" % second
        if second[0] == "plip":
            text += "net_02_promisc = 1\n"
    if NICS[card]:
        text += "\n" + NICS[card]
    if second and NICS[second[0]]:
        text += "\n" + NICS[second[0]].replace(" #1]", " #2]")
    if forwards:
        text += "\n[SLiRP Port Forwarding #1]\n"
        for i, (host, guest) in enumerate(forwards):
            text += "%d_protocol = tcp\n%d_external = %s\n%d_internal = %d\n" % (i, i, host, i, guest)
    return text


def com2_config(path):
    """COM2 on 86Box's named-pipe device, which opens a character device
    (here the bridge's pty) directly."""
    return "\n[Named Pipe (COM) #2]\npath = %s\n" % path


class Com2Bridge(threading.Thread):
    """A pty for 86Box's COM2 and a TCP port on 127.0.0.1 relaying to it.
    Everything the guest sends is also appended to `log`."""

    def __init__(self, log):
        super().__init__(daemon=True)
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)
        tty.setraw(self.master)
        self.path = os.ttyname(self.slave)
        self.lsock = socket.socket()
        self.lsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.lsock.bind(("127.0.0.1", 0))
        self.lsock.listen(1)
        self.port = self.lsock.getsockname()[1]
        self.log = open(log, "wb")
        self.conn = None
        self.stopping = False

    def run(self):
        while not self.stopping:
            fds = [self.master, self.lsock] + ([self.conn] if self.conn else [])
            try:
                ready, _, _ = select.select(fds, [], [], 0.2)
            except (OSError, ValueError):
                break
            for f in ready:
                if f is self.lsock:
                    c, _ = self.lsock.accept()
                    if self.conn:
                        self.conn.close()
                    self.conn = c
                elif f == self.master:
                    try:
                        data = os.read(self.master, 4096)
                    except OSError:
                        data = b""
                    if data:
                        self.log.write(data)
                        self.log.flush()
                        if self.conn:
                            try:
                                self.conn.sendall(data)
                            except OSError:
                                self.conn = None
                else:
                    data = self.conn.recv(4096)
                    if data:
                        os.write(self.master, data)
                    else:
                        self.conn.close()
                        self.conn = None

    def stop(self):
        self.stopping = True
        self.join(timeout=2)
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except OSError:
                pass
        self.lsock.close()
        self.log.close()


def parse_tcp_send(spec):
    """ANCHOR|TARGET|TEXT, TARGET being com2 or net:GUESTPORT."""
    anchor, target, text = spec.split("|", 2)
    return {"anchor": anchor, "target": target, "text": text}


class TcpSend(threading.Thread):
    """One --tcp-send step: connect to the host port, send TEXT and a CR LF,
    then collect what comes back until the peer closes or `quiet` seconds
    pass without data. The reply goes to `out`."""

    def __init__(self, port, text, out, delay=3.0, quiet=5.0):
        super().__init__(daemon=True)
        self.port, self.text, self.out = port, text, out
        self.delay, self.quiet = delay, quiet
        self.result = {"port": port, "sent": text, "got": 0, "error": None}

    def run(self):
        time.sleep(self.delay)
        got = b""
        try:
            s = socket.create_connection(("127.0.0.1", self.port), timeout=10)
            s.sendall(self.text.encode("latin-1") + b"\r\n")
            s.settimeout(self.quiet)
            while True:
                try:
                    data = s.recv(4096)
                except socket.timeout:
                    break
                if not data:
                    break
                got += data
            s.close()
        except OSError as e:
            self.result["error"] = str(e)
        open(self.out, "wb").write(got)
        self.result["got"] = len(got)


# --ssh-steps FILE: SSH steps against the guest (GLOS's agent), one per line:
#   wait TEXT          until TEXT is on the serial line (240 s)
#   exec COMMAND       ssh COMMAND; its stdout and stderr go to ssh-N.out/.err
#   expect rc N        the last exec's exit status was N
#   expect out TEXT    ... its stdout held TEXT
#   expect err TEXT    ... its stderr held TEXT
#   put LOCAL REMOTE   scp a host file (relative to the job's directory) to the guest
#   get REMOTE LOCAL   scp a guest file into the output directory
#   shot NAME          "glos shot" into NAME.png in the output directory
# Blank lines and lines starting with # are skipped. A failed step ends the
# list; every step is reported in result.json and ssh.log.
def parse_ssh_steps(path):
    steps = []
    for n, line in enumerate(open(path), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        verb, _, rest = line.partition(" ")
        if verb not in ("wait", "exec", "expect", "put", "get", "shot"):
            raise ValueError("%s:%d: unknown step %r" % (path, n, verb))
        if verb == "expect" and rest.split(" ", 1)[0] not in ("rc", "out", "err"):
            raise ValueError("%s:%d: expect rc|out|err" % (path, n))
        if verb in ("put", "get") and len(rest.split()) != 2:
            raise ValueError("%s:%d: %s takes two paths" % (path, n, verb))
        steps.append((n, verb, rest))
    return steps


class SshSteps(threading.Thread):
    """The --ssh-steps list, run in order with OpenSSH's ssh and scp against
    the guest's forwarded port 22 (the host key is not checked: Loop A's
    guests are fresh every run)."""

    def __init__(self, steps, port, key, user, serial, out, base):
        super().__init__(daemon=True)
        self.steps, self.port, self.key, self.user = steps, port, key, user
        self.serial, self.out, self.base = serial, out, base
        self.results, self.failed = [], False
        self.opts = ["-o", "IdentitiesOnly=yes", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=no",
                     "-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR", "-o", "ConnectTimeout=60",
                     "-i", key]

    def _run(self, argv, timeout=600):
        import subprocess
        try:
            p = subprocess.run(argv, capture_output=True, timeout=timeout)
            return p.returncode, p.stdout, p.stderr
        except (OSError, subprocess.TimeoutExpired) as e:
            return -1, b"", str(e).encode()

    def run(self):
        import shutil
        import tempfile
        if not shutil.which("ssh"):
            self.results.append({"line": 0, "step": "ssh", "ok": False, "note": "no ssh client"})
            self.failed = True
            return
        tmp = tempfile.mkdtemp()
        key = os.path.join(tmp, "key")         # ssh wants a private key only its owner can read
        shutil.copyfile(self.key, key)
        os.chmod(key, 0o600)
        self.opts[-1] = key
        try:
            self._steps()
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def _steps(self):
        last = (None, b"", b"")
        n_exec = 0
        log = open(os.path.join(self.out, "ssh.log"), "w")
        target = "%s@127.0.0.1" % self.user
        for n, verb, rest in self.steps:
            ok, note, t0 = True, "", time.time()
            if verb == "wait":
                while time.time() - t0 < 240:
                    if rest in open(self.serial, "rb").read().decode("latin-1"):
                        break
                    time.sleep(0.5)
                else:
                    ok, note = False, "not seen"
            elif verb in ("exec", "shot"):
                cmd = rest if verb == "exec" else "glos shot"
                last = self._run(["ssh", "-p", str(self.port)] + self.opts + [target, cmd])
                n_exec += 1
                if verb == "exec":
                    open(os.path.join(self.out, "ssh-%d.out" % n_exec), "wb").write(last[1])
                    open(os.path.join(self.out, "ssh-%d.err" % n_exec), "wb").write(last[2])
                    note = "rc=%d" % last[0]
                else:
                    open(os.path.join(self.out, rest + ".png"), "wb").write(last[1])
                    ok = last[0] == 0 and last[1][:8] == b"\x89PNG\r\n\x1a\n"
                    note = "%d bytes" % len(last[1])
            elif verb == "expect":
                what, _, val = rest.partition(" ")
                if what == "rc":
                    ok = last[0] == int(val)
                else:
                    ok = val.encode("latin-1") in (last[1] if what == "out" else last[2])
                note = "rc=%s" % last[0]
            else:
                a, b = rest.split()
                if verb == "put":
                    src, dst = os.path.join(self.base, a), "%s:%s" % (target, b)
                else:
                    src, dst = "%s:%s" % (target, a), os.path.join(self.out, b)
                rc, _, err = self._run(["scp", "-P", str(self.port)] + self.opts + [src, dst])
                ok, note = rc == 0, "rc=%d %s" % (rc, err.decode("latin-1").strip()[-120:])
            self.results.append({"line": n, "step": "%s %s" % (verb, rest), "ok": ok,
                                 "secs": round(time.time() - t0, 1), "note": note})
            log.write("%-4s %3d %s %s (%s)\n" % ("ok" if ok else "FAIL", n, verb, rest, note))
            log.flush()
            if not ok:
                self.failed = True
                break
        log.close()
