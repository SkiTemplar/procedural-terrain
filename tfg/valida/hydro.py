"""E0 — métricas hidrológicas: extracción de red de drenaje D8 con WhiteboxTools
(mismo pipeline y mismo umbral para referencia y predicción) y comparación de
redes con tolerancia espacial (informe §5).

Pipeline por DEM: fill depressions → D8 pointer → D8 flow accumulation →
extract streams (área ≥ umbral) → orden de Strahler.
"""

from pathlib import Path

import numpy as np
import rasterio
from scipy.ndimage import binary_dilation

DRAINAGE_AREA_KM2 = 1.0
TOLERANCES_M = (10.0, 30.0, 60.0)


def _read_mask(path: Path) -> np.ndarray:
    with rasterio.open(path) as ds:
        band = ds.read(1)
        nodata = ds.nodata if ds.nodata is not None else 0
    return (band != nodata) & (band > 0)


def extract_network(dem_path: Path, out_dir: Path, prefix: str, cell: float,
                    threshold_km2: float = DRAINAGE_AREA_KM2) -> dict:
    """Corre el pipeline D8 de Whitebox sobre un DEM. Devuelve rutas de salida."""
    import whitebox

    wbt = whitebox.WhiteboxTools()
    wbt.verbose = False
    # Un solo hilo: multihilo resuelve empates de flujo en orden no
    # determinista y el F1 de red bailaba en el 3er decimal entre ejecuciones
    # idénticas. La reproducibilidad manda sobre los segundos que cuesta.
    wbt.set_max_procs(1)
    wbt.set_working_dir(str(out_dir.resolve()))

    dem = str(dem_path.resolve())
    filled = f"{prefix}_filled.tif"
    pointer = f"{prefix}_d8pntr.tif"
    accum = f"{prefix}_facc.tif"
    streams = f"{prefix}_streams.tif"
    strahler = f"{prefix}_strahler.tif"

    threshold_cells = threshold_km2 * 1e6 / (cell * cell)
    if wbt.fill_depressions(dem, filled, fix_flats=True) != 0:
        raise RuntimeError(f"fill_depressions fallo para {prefix}")
    if wbt.d8_pointer(filled, pointer) != 0:
        raise RuntimeError(f"d8_pointer fallo para {prefix}")
    if wbt.d8_flow_accumulation(filled, accum, out_type="cells") != 0:
        raise RuntimeError(f"d8_flow_accumulation fallo para {prefix}")
    if wbt.extract_streams(accum, streams, threshold_cells) != 0:
        raise RuntimeError(f"extract_streams fallo para {prefix}")
    if wbt.strahler_stream_order(pointer, streams, strahler, zero_background=True) != 0:
        raise RuntimeError(f"strahler_stream_order fallo para {prefix}")

    return {
        "streams": out_dir / streams,
        "strahler": out_dir / strahler,
        "pointer": out_dir / pointer,
        "facc": out_dir / accum,
        "filled": out_dir / filled,
        "umbral_celdas": threshold_cells,
    }


def compare_networks(ref_streams: Path, pred_streams: Path, cell: float) -> dict:
    """Precisión/exhaustividad/F1 de la red predicha frente a la de referencia
    a varias tolerancias, más densidad de drenaje relativa."""
    ref = _read_mask(ref_streams)
    pred = _read_mask(pred_streams)

    out = {
        "celdas_red_ref": int(ref.sum()),
        "celdas_red_pred": int(pred.sum()),
        "densidad_relativa": float(pred.sum() / max(ref.sum(), 1)),
        "tolerancias": {},
    }
    for tol in TOLERANCES_M:
        it = max(1, int(round(tol / cell)))
        ref_buf = binary_dilation(ref, iterations=it)
        pred_buf = binary_dilation(pred, iterations=it)
        precision = float((pred & ref_buf).sum() / max(pred.sum(), 1))
        recall = float((ref & pred_buf).sum() / max(ref.sum(), 1))
        f1 = 2 * precision * recall / max(precision + recall, 1e-12)
        out["tolerancias"][f"{tol:.0f} m"] = {
            "precision": round(precision, 4),
            "recall": round(recall, 4),
            "F1": round(f1, 4),
        }
    return out


def strahler_histogram(strahler_path: Path) -> dict:
    with rasterio.open(strahler_path) as ds:
        band = ds.read(1)
        nodata = ds.nodata if ds.nodata is not None else 0
    valid = band[(band != nodata) & (band > 0)].astype(int)
    orders, counts = np.unique(valid, return_counts=True)
    return {f"orden_{o}": int(c) for o, c in zip(orders, counts)}
