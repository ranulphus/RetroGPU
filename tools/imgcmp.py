#!/usr/bin/env python3
"""Compare two RGB images with a per-channel tolerance (stdlib only).

Both images are quantised to RGB565 first. Options:
  tol       per-channel difference (0-255) a pixel may have and still match
  frac      fraction of pixels allowed beyond tol
  edge      ignore pixels on a colour edge of the reference (1-px border)
  box       compare 4x4 box-filtered images (stipple translucency on G100)
  ignore    rectangles [x0, y0, x1, y1) excluded from the comparison: known
            differences of the reference card, documented in the manifest
  cells     a grid {x0, y0, w, h, dx, dy, cols, n, approx: [...]}: each cell
            is judged on its own; cells listed in approx are reported but do
            not gate (G100 approximations, i.e. the combine-coverage table)
Writes an optional diff image (red where beyond tol, grey reference).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "loopa"))
import png  # noqa: E402


def q565(rgb):
    out = bytearray(len(rgb))
    for i in range(0, len(rgb), 3):
        r, g, b = rgb[i] >> 3, rgb[i + 1] >> 2, rgb[i + 2] >> 3
        out[i], out[i + 1], out[i + 2] = (r * 255) // 31, (g * 255) // 63, (b * 255) // 31
    return bytes(out)


def box4(w, h, rgb):
    """Each pixel the mean (rounded down) of the 4x4 block from (x-1, y-1) to
    (x+2, y+2), clipped to the image: sums along rows, then down columns."""
    rows = [None] * h                           # each row's 4-wide sums, per channel interleaved
    cx = [min(x + 3, w) - max(x - 1, 0) for x in range(w)]
    for y in range(h):
        r = rgb[y * w * 3:(y + 1) * w * 3]
        pre = [0, 0, 0]
        p = [[0] * (w + 1) for _ in range(3)]
        for x in range(w):
            for c in range(3):
                pre[c] += r[x * 3 + c]
                p[c][x + 1] = pre[c]
        row = [0] * (w * 3)
        for x in range(w):
            a, b = max(x - 1, 0), min(x + 3, w)
            for c in range(3):
                row[x * 3 + c] = p[c][b] - p[c][a]
        rows[y] = row
    out = bytearray(len(rgb))
    for y in range(h):
        y0, y1 = max(y - 1, 0), min(y + 3, h)
        ny = y1 - y0
        acc = [0] * (w * 3)
        for yy in range(y0, y1):
            ry = rows[yy]
            for i in range(w * 3):
                acc[i] += ry[i]
        base = y * w * 3
        for x in range(w):
            n = cx[x] * ny
            i = x * 3
            out[base + i], out[base + i + 1], out[base + i + 2] = acc[i] // n, acc[i + 1] // n, acc[i + 2] // n
    return bytes(out)


def edge_mask(w, h, rgb):
    m = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 3
            c = rgb[i:i + 3]
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < w and 0 <= yy < h:
                    j = (yy * w + xx) * 3
                    if max(abs(rgb[j + k] - c[k]) for k in range(3)) > 24:
                        m[y * w + x] = 1
                        break
    return m


def compare(ref_path, got_path, tol=24, frac=0.005, edge=True, box=False, diff_path=None, ignore=(), cells=None):
    rw, rh, ref = png.read_png(ref_path)
    gw, gh, got = png.read_png(got_path)
    if (rw, rh) != (gw, gh):
        return dict(ok=False, reason="size %dx%d vs %dx%d" % (rw, rh, gw, gh))
    ref, got = q565(ref), q565(got)
    if box:
        ref, got = box4(rw, rh, ref), box4(rw, rh, got)
    mask = edge_mask(rw, rh, ref) if edge else None
    bad = 0
    worst = 0
    exact = 0
    counted = 0
    diff = bytearray(len(ref)) if diff_path else None
    skip = None
    if ignore:
        skip = bytearray(rw * rh)
        for x0, y0, x1, y1 in ignore:
            for y in range(max(0, y0), min(rh, y1)):
                for x in range(max(0, x0), min(rw, x1)):
                    skip[y * rw + x] = 1
    for p in range(rw * rh):
        i = p * 3
        if skip is not None and skip[p]:
            if diff is not None:
                diff[i] = diff[i + 1] = diff[i + 2] = 0
            continue
        d = max(abs(ref[i] - got[i]), abs(ref[i + 1] - got[i + 1]), abs(ref[i + 2] - got[i + 2]))
        if mask is not None and mask[p]:
            if diff is not None:
                diff[i] = diff[i + 1] = diff[i + 2] = 40
            continue
        counted += 1
        if d == 0:
            exact += 1
        worst = max(worst, d)
        if d > tol:
            bad += 1
            if diff is not None:
                diff[i], diff[i + 1], diff[i + 2] = 255, 0, 0
        elif diff is not None:
            g = (ref[i] + ref[i + 1] + ref[i + 2]) // 6
            diff[i] = diff[i + 1] = diff[i + 2] = g
    if diff_path:
        png.write_png(diff_path, rw, rh, bytes(diff))
    f = bad / max(counted, 1)
    res = dict(ok=f <= frac, bad=bad, frac=round(f, 6), worst=worst,
               exact=round(exact / max(counted, 1), 4), counted=counted)
    if cells:
        res.update(cell_report(rw, ref, got, tol, frac, mask, skip, cells))
    return res


def cell_report(w, ref, got, tol, frac, mask, skip, c):
    out, ok, unexpected = {}, True, []
    approx = set(c.get("approx", []))
    for i in range(c["n"]):
        x0 = c["x0"] + (i % c["cols"]) * c["dx"]
        y0 = c["y0"] + (i // c["cols"]) * c["dy"]
        bad = n = 0
        for y in range(y0, y0 + c["h"]):
            for x in range(x0, x0 + c["w"]):
                p = y * w + x
                if (mask is not None and mask[p]) or (skip is not None and skip[p]):
                    continue
                j = p * 3
                n += 1
                if max(abs(ref[j] - got[j]), abs(ref[j + 1] - got[j + 1]), abs(ref[j + 2] - got[j + 2])) > tol:
                    bad += 1
        cf = bad / max(n, 1)
        good = cf <= frac
        out[str(i)] = dict(frac=round(cf, 4), ok=good, approx=i in approx)
        if not good and i not in approx:
            ok = False
        if good and i in approx:
            unexpected.append(i)
    return dict(ok=ok, cells=out, approx_but_passing=unexpected)


if __name__ == "__main__":
    import argparse
    import json
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("got")
    ap.add_argument("--tol", type=int, default=24)
    ap.add_argument("--frac", type=float, default=0.005)
    ap.add_argument("--no-edge", action="store_true")
    ap.add_argument("--box", action="store_true")
    ap.add_argument("--diff")
    a = ap.parse_args()
    r = compare(a.ref, a.got, a.tol, a.frac, not a.no_edge, a.box, a.diff)
    print(json.dumps(r))
    sys.exit(0 if r.get("ok") else 1)
