#!/usr/bin/env python3
"""Split a DOSBench serial log's profile lines by test.

  profsplit.py SERIAL.LOG OUTDIR

A PROF=1 or PROF=2 DOS-GL program run with DGL_STATS=2 prints DGL-PROF,
DGL-PFINE and DGL-PCNT lines a second; BENCHGL ends each test with an
"HX-TEST <ID> ..." line. The profile lines before a test's HX-TEST line (and
after the previous one) go to OUTDIR/<ID>.log, for profsum.py (with --skip 1:
the first second holds the test's loading).
"""
import os
import sys


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().split("\n\n")[1].strip(), file=sys.stderr)
        return 2
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    cur, n = [], 0
    for line in open(src, errors="replace"):
        if line.startswith(("DGL-PROF", "DGL-PFINE", "DGL-PCNT")):
            cur.append(line)
        elif line.startswith("HX-TEST "):
            test = line.split()[1]
            with open(os.path.join(out, test + ".log"), "w") as f:
                f.writelines(cur)
            cur, n = [], n + 1
    print("profsplit: %d tests -> %s" % (n, out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
