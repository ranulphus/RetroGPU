#!/usr/bin/env python3
"""A bench PC reached through GLOS's agent (GLOS PRD §7): Loop B's SSH
transport, used by tools/bench/run.py when the PC answers on its SSH port.

GLOS runs on the PC instead of the BENCH.BAT poller and owns the network
card. A job's files go to C:\\TEST by SFTP, RUN.BAT runs as an agent job,
the PC's COM1 lines come back through `glos log --follow` (so no serial
cable is needed), and C:\\OUT is fetched by SFTP. Every command of a job
shares one connection (OpenSSH's ControlMaster).

  glosssh.py HOST[:PORT] KEY up            exit 0 if GLOS answers there
  glosssh.py HOST[:PORT] KEY COMMAND...    run a command, show what it says
"""
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading


class Glos:
    def __init__(self, target, key):
        host, _, port = target.partition(":")
        self.host, self.port = host, int(port or 22)
        self.tmp = tempfile.mkdtemp(prefix="glosssh-")
        # ssh wants a private key no one else can read; a checked-out test key may be 0644.
        self.key = os.path.join(self.tmp, "key")
        shutil.copyfile(os.path.expanduser(key), self.key)
        os.chmod(self.key, 0o600)
        self.opts = ["-o", "IdentitiesOnly=yes", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=no",
                     "-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR", "-o", "ConnectTimeout=8",
                     "-o", "ControlMaster=auto", "-o", "ControlPath=" + os.path.join(self.tmp, "cm"),
                     "-o", "ControlPersist=3600", "-i", self.key]
        self.dest = "glos@" + self.host
        self.follower = None

    def ssh(self, command, timeout=60):
        """(exit code, stdout, stderr); the code is 255 when ssh itself failed, -1 on a timeout."""
        try:
            p = subprocess.run(["ssh", "-p", str(self.port)] + self.opts + [self.dest, command],
                               capture_output=True, timeout=timeout, stdin=subprocess.DEVNULL)
            return p.returncode, p.stdout.decode("latin-1"), p.stderr.decode("latin-1")
        except subprocess.TimeoutExpired:
            return -1, "", "timeout"

    def up(self):
        rc, out, _ = self.ssh("glos ver", timeout=20)
        return rc == 0 and out.startswith("GLOS")

    def start(self, command):
        """An agent job, not waited for: its Popen (stdout and stderr together, bytes)."""
        return subprocess.Popen(["ssh", "-p", str(self.port)] + self.opts + [self.dest, command],
                                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def sftp(self, lines, timeout=1800):
        """A batch of sftp commands; a line starting with - may fail. (exit code, what it said)."""
        batch = os.path.join(self.tmp, "batch")
        with open(batch, "w") as f:
            f.write("\n".join(lines) + "\n")
        try:
            p = subprocess.run(["sftp", "-P", str(self.port)] + self.opts + ["-b", batch, self.dest],
                               capture_output=True, timeout=timeout)
            return p.returncode, (p.stdout + p.stderr).decode("latin-1")
        except subprocess.TimeoutExpired:
            return -1, "timeout"

    def put(self, files, remote="/C/TEST"):
        """Local files into a directory on the PC, emptied first. files: (local path, DOS name)."""
        lines = ["-mkdir " + remote, "-rm %s/*" % remote]
        lines += ["put %s %s/%s" % (src, remote, name) for src, name in files]
        return self.sftp(lines)

    def get_dir(self, remote, local, timeout=300):
        """Every file of a directory on the PC; the names fetched."""
        os.makedirs(local, exist_ok=True)
        rc, said = self.sftp(["-get %s/* %s/" % (remote, local)], timeout=timeout)
        return sorted(os.listdir(local))

    def follow(self):
        """The PC's log from now on, as COM1 would give it: lines through poll()."""
        self.lines = queue.Queue()
        # --follow starts with the last 16 KB; what was there before this job is not its own.
        mark = "HX-FOLLOW %d" % os.getpid()
        self.ssh("glos echo " + mark)
        self.follower = subprocess.Popen(["ssh", "-p", str(self.port)] + self.opts + [self.dest, "glos log --follow"],
                                         stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)

        def reader():
            seen = False
            for raw in iter(self.follower.stdout.readline, b""):
                line = raw.decode("latin-1").rstrip("\r\n")
                if seen:
                    self.lines.put(line)
                elif mark in line:              # (GLOS logs each command: "GLOS-SSH exec=...")
                    seen = True
        threading.Thread(target=reader, daemon=True).start()
        return self

    def poll(self):
        got = []
        while True:
            try:
                got.append(self.lines.get_nowait())
            except queue.Empty:
                return got

    def close(self):
        if self.follower:
            self.follower.terminate()
        try:
            subprocess.run(["ssh", "-p", str(self.port)] + self.opts + ["-O", "exit", self.dest],
                           capture_output=True, timeout=30)
        except subprocess.TimeoutExpired:       # (a PC gone down: nothing left to close politely)
            pass
        shutil.rmtree(self.tmp, ignore_errors=True)


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    g = Glos(sys.argv[1], sys.argv[2])
    try:
        if sys.argv[3] == "up":
            return 0 if g.up() else 1
        rc, out, err = g.ssh(" ".join(sys.argv[3:]))
        sys.stdout.write(out)
        sys.stderr.write(err)
        return rc
    finally:
        g.close()


if __name__ == "__main__":
    sys.exit(main())
