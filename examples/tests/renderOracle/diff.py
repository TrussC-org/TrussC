#!/usr/bin/env python3
# Image diff for the renderOracle regression check.
#   python3 diff.py A.bmp B.bmp [--save-diff out.png] [--tol N]
# Prints: dimensions, exact-match, #differing pixels (and %), max per-channel error,
# mean error, and a per-channel max. Exit 0 if identical within --tol, else 1.
import sys, numpy as np
from PIL import Image

def load(p):
    return np.asarray(Image.open(p).convert("RGB")).astype(np.int32)

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        print("usage: diff.py A.bmp B.bmp [--save-diff out.png] [--tol N]"); return 2
    tol = 0
    if "--tol" in sys.argv: tol = int(sys.argv[sys.argv.index("--tol")+1])
    A, B = load(args[0]), load(args[1])
    if A.shape != B.shape:
        print(f"DIMENSION MISMATCH: {A.shape} vs {B.shape}  -> FAIL"); return 1

    d = np.abs(A - B)                      # per-pixel per-channel abs error
    pix = d.max(axis=2)                    # per-pixel max-channel error
    ndiff = int((pix > tol).sum())
    total = pix.size
    maxerr = int(d.max())
    meanerr = float(d.mean())
    perch = d.reshape(-1, 3).max(axis=0)   # max error per R,G,B

    h, w, _ = A.shape
    print(f"size           : {w}x{h}")
    print(f"exact match    : {'YES' if maxerr == 0 else 'no'}")
    print(f"diff pixels    : {ndiff} / {total}  ({100.0*ndiff/total:.4f}%)   (tol={tol})")
    print(f"max error      : {maxerr}   per-channel R/G/B = {perch[0]},{perch[1]},{perch[2]}")
    print(f"mean error     : {meanerr:.4f}")
    if ndiff:
        ys, xs = np.where(pix > tol)
        # bounding box of differences — helps localize which region changed
        print(f"diff bbox      : x[{xs.min()}..{xs.max()}] y[{ys.min()}..{ys.max()}]")

    if "--save-diff" in sys.argv:
        outp = sys.argv[sys.argv.index("--save-diff")+1]
        vis = np.clip(d.sum(axis=2) * 8, 0, 255).astype(np.uint8)  # amplified diff heatmap
        Image.fromarray(vis).save(outp); print(f"diff heatmap   : {outp}")

    ok = ndiff == 0
    print("RESULT         :", "PASS" if ok else "FAIL")
    return 0 if ok else 1

sys.exit(main())
