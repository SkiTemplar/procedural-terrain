"""E0 — orquestador: números de referencia de la bicúbica (GLO-30 → 2 m)
contra el MDT02 del PNOA (hoja MTN50 180-1, Benasque/Posets).

Salidas: e0/out/resultados-e0.json + e0/out/verificacion-visual.png
"""

import json
import time
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import rasterio
from matplotlib.colors import LightSource
from scipy.ndimage import binary_dilation

from hydro import compare_networks, extract_network, strahler_histogram
from metrics import derivative_metrics, elevation_metrics
from prepare import MDT02, PRED_BICUBIC, build_bicubic_prediction, load_pair

OUT_DIR = Path("e0/out")


def render_visual(ref, pred, mask, ref_streams, pred_streams, cell) -> Path:
    ls = LightSource(azdeg=315, altdeg=45)
    fig, axes = plt.subplots(1, 2, figsize=(16, 6.5), dpi=110)
    for ax, z, streams_path, title in (
        (axes[0], ref, ref_streams, "MDT02 PNOA 2 m (referencia)"),
        (axes[1], pred, pred_streams, "GLO-30 bicubica a 2 m (baseline E0)"),
    ):
        zz = np.where(mask, z, np.nan)
        hs = ls.hillshade(np.nan_to_num(zz, nan=float(np.nanmean(zz))), dx=cell, dy=cell)
        ax.imshow(hs, cmap="gray", interpolation="nearest")
        with rasterio.open(streams_path) as ds:
            s = ds.read(1)
            s_nodata = ds.nodata if ds.nodata is not None else 0
        stream_mask = (s != s_nodata) & (s > 0)
        # una linea de 1 celda desaparece al reducir 6820 px -> ~900 px: dilatar solo para display
        stream_mask = binary_dilation(stream_mask, iterations=4)
        red = np.zeros((*s.shape, 4), dtype=np.float32)
        red[stream_mask] = (0.9, 0.15, 0.1, 1.0)
        ax.imshow(red, interpolation="nearest")
        ax.set_title(title)
        ax.axis("off")
    fig.suptitle("E0 — red de drenaje D8 (area >= 1 km2) sobre hillshade")
    fig.tight_layout()
    out = OUT_DIR / "verificacion-visual.png"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    return out


def main() -> None:
    t0 = time.time()
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    print("[1/5] remuestreo bicubico GLO-30 -> malla 2 m del MDT02...")
    build_bicubic_prediction()
    ref, pred, mask, cell = load_pair()
    print(f"      malla {ref.shape[1]}x{ref.shape[0]} @ {cell} m, validas: {mask.sum()}")

    print("[2/5] metricas de elevacion...")
    res = {"zona": "MTN50 0180-1 (Benasque/Posets), EPSG:25831",
           "entrada": "Copernicus GLO-30 (AWS, tile 2022) remuestreada bicubica",
           "referencia": "PNOA MDT02 2a cobertura (CNIG, 2021)",
           "elevacion": elevation_metrics(ref, pred, mask)}

    print("[3/5] metricas de derivados (slope/aspect)...")
    res["derivados"] = derivative_metrics(ref, pred, mask, cell)

    print("[4/5] redes de drenaje D8 (WhiteboxTools)...")
    net_ref = extract_network(MDT02, OUT_DIR, "ref", cell)
    net_pred = extract_network(PRED_BICUBIC, OUT_DIR, "pred", cell)
    res["hidrologia"] = compare_networks(net_ref["streams"], net_pred["streams"], cell)
    res["hidrologia"]["umbral_area_km2"] = 1.0
    res["hidrologia"]["strahler_ref"] = strahler_histogram(net_ref["strahler"])
    res["hidrologia"]["strahler_pred"] = strahler_histogram(net_pred["strahler"])

    print("[5/5] verificacion visual...")
    png = render_visual(ref, pred, mask, net_ref["streams"], net_pred["streams"], cell)

    res["duracion_s"] = round(time.time() - t0, 1)
    out_json = OUT_DIR / "resultados-e0.json"
    out_json.write_text(json.dumps(res, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\nresultados: {out_json}\nvisual: {png}")
    print(json.dumps(res, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
