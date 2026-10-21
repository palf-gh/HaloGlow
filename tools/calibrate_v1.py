#!/usr/bin/env python3
"""Fit the provisional HaloGlow coefficient table (algorithm version 1).

The basis impulse responses come from the shipped C++ pyramid (hg_impulse), so
the table is fitted to the filter the plug-in actually runs (spec section 7.4).

Bases: the level-0 delta, four local FIRs (sigma 0.5/0.75/1.0/1.5 px), every
pyramid level D_j and its half-octave companion Bin5(D_j).

Fit: for each Falloff in {0,25,50,75,100} and log2(R90) on a 1/4-octave grid,
solve a sum-constrained non-negative least squares problem with the objective
E_profile + E_EEF + E_2D (weights 1:10:1) averaged over a fixed phase set.
log2 R <= 4.75 uses a 1024^2 impulse set with six phases; up to 7.25 a 2048^2
set with two phases (E_2D on a strided pixel subset).

Above log2 R = 7.25 the table repeats the [6.5, 7.25] octave shifted by whole
levels (a_j(2^k R) = a_{j-k}(R)). That range uses no delta, local FIR or fine
level, and level responses are self-similar there (sigma_j / 2^j = 1.222 ..
1.225 for j >= 3); the generator refuses a shift otherwise. This remains an
assumption of version 1, checked by the C++ shape tests (spec section 17).

Usage:
  python tools/calibrate_v1.py --impulse build/Win/Release/hg_impulse.exe
"""

import argparse
import hashlib
import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.optimize import nnls
from scipy.special import erf

ROOT = Path(__file__).resolve().parents[1]
MAX_LEVELS = 16
LOCAL_BASES = 4
COEFFS = 2 * (1 + MAX_LEVELS) + LOCAL_BASES
EEF_WEIGHT = 10.0
FALLOFFS = [0.0, 25.0, 50.0, 75.0, 100.0]
LOG2_MIN, LOG2_STEP, LOG2_MAX = -1.0, 0.25, 11.0
SMALL = dict(size=1024, levels=8, phases=[(0, 0), (1, 0), (0, 1), (1, 1), (2, 3), (3, 2)], top=4.75)
LARGE = dict(size=2048, levels=10, phases=[(0, 0), (1, 1)], top=7.25)
SHIFT_BASE = 6.5
MOFFAT_A, MOFFAT_BETA, SUPPORT = 0.80, 2.5, 4.0


def weights(f):
    f /= 100.0
    return 0.75 - 0.30 * f, 0.23 + 0.14 * f, 0.02 + 0.16 * f


def moffat_mass(r):
    return 1.0 - (1.0 + (r / MOFFAT_A) ** 2) ** (1.0 - MOFFAT_BETA)


def gauss_mass(r, s):
    return 1.0 - math.exp(-r * r / (2 * s * s))


def q90(falloff):
    wc, wm, wh = weights(falloff)
    total = wc * gauss_mass(SUPPORT, 0.10) + wm * gauss_mass(SUPPORT, 0.40) + wh * moffat_mass(SUPPORT)
    lo, hi = 0.0, SUPPORT
    for _ in range(200):
        mid = 0.5 * (lo + hi)
        m = (wc * gauss_mass(mid, 0.10) + wm * gauss_mass(mid, 0.40) + wh * moffat_mass(mid)) / total
        lo, hi = (mid, hi) if m < 0.9 else (lo, mid)
    return 0.5 * (lo + hi)


def erf_cells(half, sigma):
    """Pixel integrals of a 1-D Gaussian over cells centred on -half..half."""
    x = np.arange(-half, half + 1, dtype=np.float64)
    k = 1.0 / (math.sqrt(2.0) * sigma)
    return 0.5 * (erf((x + 0.5) * k) - erf((x - 0.5) * k))


def target_psf(radius, falloff, half):
    """Pixel-area integrated, truncated and normalised target K_{R,F}."""
    wc, wm, wh = weights(falloff)
    scale = radius / q90(falloff)
    gc = erf_cells(half, 0.10 * scale)
    gm = erf_cells(half, 0.40 * scale)
    img = wc * np.outer(gc, gc) + wm * np.outer(gm, gm)
    n = int(min(32, max(2, math.ceil(4.0 / max(MOFFAT_A * scale, 1e-3)))))
    off = (np.arange(n) + 0.5) / n - 0.5
    x = np.arange(-half, half + 1, dtype=np.float64)
    acc = np.zeros((2 * half + 1, 2 * half + 1))
    a = MOFFAT_A * scale
    norm = (MOFFAT_BETA - 1.0) / (math.pi * a * a)
    for oy in off:
        yy = (x + oy)[:, None] ** 2
        for ox in off:
            rr = np.sqrt((x + ox)[None, :] ** 2 + yy)
            m = norm * (1.0 + (rr / a) ** 2) ** (-MOFFAT_BETA)
            m[rr > SUPPORT * scale] = 0.0
            acc += m
    img += wh * acc / (n * n)
    return img / img.sum()


def radial_tools(half):
    x = np.arange(-half, half + 1, dtype=np.float64)
    rr = np.sqrt(x[None, :] ** 2 + x[:, None] ** 2).ravel()
    order = np.argsort(rr, kind="stable")
    return rr, order


def eef_curve(flat, rr, order, radii):
    cum = np.cumsum(flat[order])
    idx = np.searchsorted(rr[order], radii, side="right") - 1
    return np.where(idx >= 0, cum[np.clip(idx, 0, None)], 0.0)


def profile(flat, rr, nbins):
    b = np.minimum(rr.astype(np.int64), nbins - 1)
    s = np.bincount(b, weights=flat, minlength=nbins)
    c = np.bincount(b, minlength=nbins)
    return s / np.maximum(c, 1)


def r90_of(flat, rr, order):
    cum = np.cumsum(flat[order])
    k = np.searchsorted(cum, 0.9 * cum[-1])
    return rr[order][min(k, len(rr) - 1)]


class BasisSet:
    """Memory-mapped impulse responses written by hg_impulse."""

    def __init__(self, work, cfg):
        self.size = cfg["size"]
        self.levels = cfg["levels"]
        self.phases = cfg["phases"]
        self.top = cfg["top"]
        info = json.loads((work / "manifest.json").read_text())
        self.count = info["basis_count"]
        if info["local_count"] != LOCAL_BASES or self.count != 1 + LOCAL_BASES + 2 * self.levels:
            raise SystemExit("hg_impulse basis layout does not match the calibrator")
        self.data = [[np.memmap(work / f"basis_{p}_{k}.f32", dtype="<f4", mode="r",
                                shape=(self.size, self.size)) for k in range(self.count)]
                     for p in range(len(self.phases))]

    def crop(self, p, k, half):
        px, py = self.phases[p]
        cx, cy = self.size // 2 + px, self.size // 2 + py
        return np.asarray(self.data[p][k][cy - half:cy + half + 1, cx - half:cx + half + 1], dtype=np.float64)

    def close(self):
        self.data = None


def fit(bs, radius, falloff):
    q = q90(falloff)
    half = int(min(bs.size // 2 - 8, math.ceil(SUPPORT * radius / q) + 12))
    tflat = target_psf(radius, falloff, half).ravel()
    rr, order = radial_tools(half)
    radii = np.unique(np.concatenate([np.linspace(0.0, half, 64), np.geomspace(0.5, half, 32)]))
    nb = half + 1
    t_eef = eef_curve(tflat, rr, order, radii)
    t_prof = profile(tflat, rr, nb)
    peak = tflat.max()
    # E_2D rows on a strided pixel subset for large windows (memory), plus
    # every pixel of the core; normalisation keeps it a relative L2 error.
    stride = max(1, half // 192)
    sub = np.zeros((2 * half + 1, 2 * half + 1), dtype=bool)
    sub[::stride, ::stride] = True
    sub[half - 8:half + 9, half - 8:half + 9] = True
    sub = sub.ravel()
    tsub = tflat[sub]
    nph = len(bs.phases)

    rows_a, rows_b = [], []
    for p in range(nph):
        A = np.stack([bs.crop(p, k, half).ravel() for k in range(bs.count)], axis=1)
        w2d = 1.0 / math.sqrt(np.sum(tsub ** 2) * nph)
        rows_a.append(A[sub] * w2d)
        rows_b.append(tsub * w2d)
        we = EEF_WEIGHT / math.sqrt(len(radii) * nph)
        rows_a.append(np.stack([eef_curve(A[:, k], rr, order, radii) for k in range(bs.count)], axis=1) * we)
        rows_b.append(t_eef * we)
        wp = 1.0 / (peak * math.sqrt(nb * nph))
        rows_a.append(np.stack([profile(A[:, k], rr, nb) for k in range(bs.count)], axis=1) * wp)
        rows_b.append(t_prof * wp)
        del A
    big = 1.0e3
    rows_a.append(np.ones((1, bs.count)) * big)
    rows_b.append(np.array([big]))
    x, _ = nnls(np.concatenate(rows_a), np.concatenate(rows_b), maxiter=5000)
    x = x / x.sum()

    metrics = {"energy_err": 0.0, "r90_rel_err": 0.0, "eef_max": 0.0, "e2d": 0.0}
    t_r90 = r90_of(tflat, rr, order)
    for p in range(nph):
        af = np.zeros_like(tflat)
        for k in range(bs.count):
            if x[k] > 0.0:
                af += x[k] * bs.crop(p, k, half).ravel()
        metrics["energy_err"] = max(metrics["energy_err"], abs(af.sum() - 1.0))
        metrics["r90_rel_err"] = max(metrics["r90_rel_err"], abs(r90_of(af, rr, order) - t_r90) / radius)
        metrics["eef_max"] = max(metrics["eef_max"], float(np.max(np.abs(eef_curve(af, rr, order, radii) - t_eef))))
        metrics["e2d"] += float(np.sum((af - tflat) ** 2) / np.sum(tflat ** 2)) / nph
    return x, metrics


def to_table_row(x, levels):
    """Basis order (delta, local FIRs, a_1..a_L, c_1..c_L) -> table layout
    (a_0..a_16, c_0..c_16, b_0..b_3)."""
    n = 1 + MAX_LEVELS
    row = np.zeros(COEFFS)
    row[0] = x[0]
    row[2 * n:] = x[1:1 + LOCAL_BASES]
    o = 1 + LOCAL_BASES
    row[1:1 + levels] = x[o:o + levels]
    row[n + 1:n + 1 + levels] = x[o + levels:o + 2 * levels]
    return row


def shifted(row, k):
    """a_j(2^k R) = a_{j-k}(R), same for c. Refuses a source that still uses
    the delta or the local FIRs, which have no octave-shifted counterpart."""
    n = 1 + MAX_LEVELS
    fine = row[0] + row[2 * n:].sum()
    if fine > 1e-4:
        raise SystemExit(f"shift source still uses fine bases ({fine:.2e})")
    out = np.zeros(COEFFS)
    for base in (0, n):
        for j in range(1, n):
            v = row[base + j]
            if v == 0.0:
                continue
            if j + k > MAX_LEVELS:
                raise SystemExit(f"shift by {k} overflows level {j}")
            out[base + j + k] = v
    return out / out.sum()


def run_impulse(exe, work, cfg):
    cmd = [str(Path(exe).resolve()), str(work), str(cfg["size"]), str(cfg["levels"])]
    for px, py in cfg["phases"]:
        cmd += [str(px), str(py)]
    subprocess.run(cmd, check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--impulse", required=True, help="path to hg_impulse executable")
    ap.add_argument("--out", default=str(ROOT / "Source" / "HG_CoefficientTable_v1.inc"))
    ap.add_argument("--manifest", default=str(ROOT / "tools" / "calibration_v1_manifest.json"))
    args = ap.parse_args()

    grid = [LOG2_MIN + i * LOG2_STEP for i in range(int(round((LOG2_MAX - LOG2_MIN) / LOG2_STEP)) + 1)]
    table = np.zeros((len(FALLOFFS), len(grid), COEFFS))
    report = []
    digest = hashlib.sha256()
    with tempfile.TemporaryDirectory() as tmp:
        sets = []
        for name, cfg in (("small", SMALL), ("large", LARGE)):
            work = Path(tmp) / name
            work.mkdir()
            run_impulse(args.impulse, work, cfg)
            for f in sorted(work.glob("basis_*.f32")):
                digest.update(f.read_bytes())
            sets.append(BasisSet(work, cfg))
        small, large = sets
        for fi, falloff in enumerate(FALLOFFS):
            for si, s in enumerate(grid):
                if s <= large.top + 1e-9:
                    bs = small if s <= small.top + 1e-9 else large
                    x, m = fit(bs, 2.0 ** s, falloff)
                    table[fi, si] = to_table_row(x, bs.levels)
                    m.update({"falloff": falloff, "log2_radius": s, "fitted": True, "set": bs.size})
                    report.append(m)
                    print(f"F={falloff:5.1f} log2R={s:5.2f} EEF={m['eef_max']:.4f} "
                          f"R90={m['r90_rel_err']:.4f} E={m['energy_err']:.2e} E2D={m['e2d']:.4f}", flush=True)
                else:
                    k = int(math.floor(s - SHIFT_BASE + 1e-9))
                    base = grid.index(s - k)
                    table[fi, si] = shifted(table[fi, base], k)
                    report.append({"falloff": falloff, "log2_radius": s, "fitted": False, "shift_from": s - k})
        for bs in sets:
            bs.close()
        del sets, small, large

    lines = [
        "// Generated by tools/calibrate_v1.py - do not edit by hand.",
        "// Algorithm version 1 (provisional calibration, spec section 17).",
        f"// Basis sha256: {digest.hexdigest()}",
        f"constexpr int kTableFalloffCount = {len(FALLOFFS)};",
        f"constexpr int kTableRadiusCount = {len(grid)};",
        f"constexpr int kTableCoefficientCount = {COEFFS};",
        f"constexpr double kTableLog2Min = {LOG2_MIN};",
        f"constexpr double kTableLog2Step = {LOG2_STEP};",
        f"constexpr double kTableLog2Max = {LOG2_MAX};",
        "const float kTable[kTableFalloffCount][kTableRadiusCount][kTableCoefficientCount] = {",
    ]
    for fi in range(len(FALLOFFS)):
        lines.append(" {")
        for si in range(len(grid)):
            vals = ", ".join(f"{v:.9g}f" if v != 0 else "0.0f" for v in table[fi, si])
            lines.append(f"  {{{vals}}},")
        lines.append(" },")
    lines.append("};")
    Path(args.out).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")

    manifest = {
        "algorithm_version": 1,
        "status": "provisional",
        "falloffs": FALLOFFS,
        "log2_radius_grid": [LOG2_MIN, LOG2_STEP, LOG2_MAX],
        "impulse_sets": [SMALL, LARGE],
        "shift_base_log2_radius": SHIFT_BASE,
        "objective": "E_profile + E_EEF + E_2D, weights 1:10:1, sum-constrained NNLS",
        "basis_sha256": digest.hexdigest(),
        "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "fits": report,
    }
    Path(args.manifest).write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8", newline="\n")
    worst = max(r["eef_max"] for r in report if r["fitted"])
    print(f"worst fitted EEF difference {worst:.4f}")


if __name__ == "__main__":
    sys.exit(main())
