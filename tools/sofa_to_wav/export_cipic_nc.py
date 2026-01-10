#!/usr/bin/env python3
"""
Export HRIR WAVs from a SOFA file for all available elevations at selected azimuths.

This version avoids pysofaconventions API differences by reading the SOFA file
directly via netCDF4 (SOFA files are NetCDF).

Outputs:
  azi_<az>_ele_<el>_L.wav
  azi_<az>_ele_<el>_R.wav

Requires:
  pip3 install netCDF4 soundfile numpy
"""

import os
import numpy as np
import soundfile as sf

try:
    from netCDF4 import Dataset
except ImportError as e:
    raise SystemExit("Missing dependency netCDF4. Install with: pip3 install netCDF4") from e


def sanitize_deg(x) -> int:
    return int(np.round(float(x)))


def export_sofa_subset_nc(
    sofa_path: str,
    out_dir: str,
    az_list_deg,
    el_target_deg: float = 0.0,
    export_all_elevations: bool = True,
    overwrite: bool = False,
    normalize: bool = False,
):
    os.makedirs(out_dir, exist_ok=True)
    az_set = set(int(a) for a in az_list_deg)

    with Dataset(sofa_path, "r") as ds:
        if "Data.IR" not in ds.variables:
            raise RuntimeError("SOFA missing variable 'Data.IR'")
        if "SourcePosition" not in ds.variables:
            raise RuntimeError("SOFA missing variable 'SourcePosition'")
        if "Data.SamplingRate" not in ds.variables:
            raise RuntimeError("SOFA missing variable 'Data.SamplingRate'")

        ir = np.array(ds.variables["Data.IR"][:])  # [M, R, N]
        if ir.ndim != 3:
            raise RuntimeError(f"Unexpected Data.IR shape: {ir.shape} (expected [M, R, N])")
        M, R, N = ir.shape
        if R < 2:
            raise RuntimeError(f"Expected at least 2 receivers (L/R). Got R={R}")

        fs_var = ds.variables["Data.SamplingRate"][:]
        fs = float(np.array(fs_var).reshape(-1)[0])

        src_pos = np.array(ds.variables["SourcePosition"][:])  # [M, C] where C>=2
        if src_pos.ndim != 2 or src_pos.shape[0] != M or src_pos.shape[1] < 2:
            raise RuntimeError(f"Unexpected SourcePosition shape: {src_pos.shape} (expected [M, >=2])")

        az_all = np.array([sanitize_deg(a) for a in src_pos[:, 0]], dtype=int)
        el_all = np.array([sanitize_deg(e) for e in src_pos[:, 1]], dtype=int)

        exported = []
        used = set()  # avoid duplicates after rounding

        for az in sorted(az_set):
            idx_az = np.where(az_all == az)[0]
            if idx_az.size == 0:
                print(f"[warn] No measurements found for azimuth {az}°")
                continue

            if export_all_elevations:
                idx_selected = idx_az
            else:
                el_t = sanitize_deg(el_target_deg)
                els_here = el_all[idx_az]
                closest_el = els_here[np.argmin(np.abs(els_here - el_t))]
                idx_selected = idx_az[els_here == closest_el]

            for i in idx_selected:
                az_i = int(az_all[i])
                el_i = int(el_all[i])
                key = (az_i, el_i)
                if key in used:
                    continue

                h_l = np.asarray(ir[i, 0, :], dtype=np.float32)
                h_r = np.asarray(ir[i, 1, :], dtype=np.float32)

                if normalize:
                    peak = max(float(np.max(np.abs(h_l))), float(np.max(np.abs(h_r))), 1e-12)
                    h_l = (h_l / peak).astype(np.float32)
                    h_r = (h_r / peak).astype(np.float32)

                fn_l = os.path.join(out_dir, f"azi_{az_i}_ele_{el_i}_L.wav")
                fn_r = os.path.join(out_dir, f"azi_{az_i}_ele_{el_i}_R.wav")

                if (not overwrite) and (os.path.exists(fn_l) or os.path.exists(fn_r)):
                    used.add(key)
                    continue

                sf.write(fn_l, h_l, int(fs), subtype="FLOAT")
                sf.write(fn_r, h_r, int(fs), subtype="FLOAT")

                used.add(key)
                exported.append(key)

    print(f"Done. Exported {len(exported)} directions into: {out_dir}")

    if exported:
        by_az = {}
        for az_i, el_i in exported:
            by_az.setdefault(az_i, set()).add(el_i)
        for az_i in sorted(by_az):
            els = sorted(by_az[az_i])
            print(f"  az {az_i:>4}°: elevations {els}")


if __name__ == "__main__":
    # Edit these two paths:
    sofa_path = "/Volumes/APFS/CIPIC_subject_119_equiangular_grid.sofa"
    out_dir = "/Users/chenzuyu/BinauralPanner/plugin/BinauralPanner/Assets/hrir_wav"

    az_list = list(range(-90, 91, 10))

    export_sofa_subset_nc(
        sofa_path,
        out_dir,
        az_list_deg=az_list,
        el_target_deg=0.0,
        export_all_elevations=True,
        overwrite=False,
        normalize=False,
    )
