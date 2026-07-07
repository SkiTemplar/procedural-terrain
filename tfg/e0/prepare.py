"""E0 — alineado: reproyecta GLO-30 (EPSG:4326, 30 m) a la malla exacta del
MDT02 (EPSG:25831, 2 m) con remuestreo bicúbico. La bicúbica ES el baseline E0."""

from pathlib import Path

import numpy as np
import rasterio
from rasterio.warp import Resampling, reproject

MDT02 = Path("e0/data/MDT02-ETRS89-HU31-0180-1-COB2.tif")
GLO30 = Path("e0/data/GLO30_N42_E000.tif")
OUT_DIR = Path("e0/out")
PRED_BICUBIC = OUT_DIR / "glo30_bicubic_2m.tif"
NODATA = -9999.0


def build_bicubic_prediction() -> Path:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    with rasterio.open(MDT02) as ref, rasterio.open(GLO30) as src:
        dst = np.full((ref.height, ref.width), NODATA, dtype=np.float32)
        reproject(
            source=rasterio.band(src, 1),
            destination=dst,
            dst_transform=ref.transform,
            dst_crs=ref.crs,
            dst_nodata=NODATA,
            resampling=Resampling.cubic,
        )
        profile = ref.profile.copy()
        profile.update(dtype="float32", nodata=NODATA, compress="deflate")
        with rasterio.open(PRED_BICUBIC, "w", **profile) as out:
            out.write(dst, 1)
    return PRED_BICUBIC


def load_pair() -> tuple[np.ndarray, np.ndarray, np.ndarray, float]:
    """Devuelve (ref, pred, máscara válida, tamaño de celda en m)."""
    if not PRED_BICUBIC.exists():
        build_bicubic_prediction()
    with rasterio.open(MDT02) as ds:
        ref = ds.read(1).astype(np.float32)
        ref_nodata = ds.nodata
        cell = ds.res[0]
    with rasterio.open(PRED_BICUBIC) as ds:
        pred = ds.read(1).astype(np.float32)
    mask = (ref != ref_nodata) & (pred != NODATA)
    return ref, pred, mask, cell


if __name__ == "__main__":
    path = build_bicubic_prediction()
    print(f"prediccion bicubica escrita: {path}")
