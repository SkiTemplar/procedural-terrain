"""T1b — ¿la red quemada + LEM fino bate a la bicúbica en F1@10 m?

Caso sintético (E1) en ventana: MDT02 (2 m) → degradar a 30 m → amplificar →
comparar contra el MDT02 original con el pipeline de validación de E0.
Criterio de éxito T1b (plan maestro C.4.1): F1@10m > bicúbica; la elevación
puede empeorar (eso se arregla en T1c con el anclaje de baja frecuencia).
"""

import json
import sys
import time
from pathlib import Path

import numpy as np
import rasterio
from rasterio.windows import Window

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from amplify.v0 import (FINE_CELL, amplify_v0, build_uplift, degrade_to_coarse,
                        rasterize_network, resample_to_fine, save_tif)
from rasterio.warp import Resampling
from valida.hydro import compare_networks, extract_network
from valida.metrics import derivative_metrics, elevation_metrics

MDT02 = Path("e0/data/MDT02-ETRS89-HU31-0180-1-COB2.tif")
REF_FACC = Path("e0/out/ref_facc.tif")  # de E0: para elegir ventana con río
OUT = Path("t1b/out")
WINDOW = 2048
THRESHOLD_KM2 = 0.5


def pick_window() -> tuple[int, int]:
    """Centra la ventana en el máximo flujo acumulado lejos de los bordes."""
    with rasterio.open(REF_FACC) as ds:
        facc = ds.read(1)
    margin = WINDOW // 2 + 64
    interior = np.zeros_like(facc, dtype=bool)
    interior[margin:-margin, margin:-margin] = True
    facc = np.where(interior, facc, 0)
    r, c = np.unravel_index(np.argmax(facc), facc.shape)
    return int(r - WINDOW // 2), int(c - WINDOW // 2)


def main() -> None:
    t0 = time.time()
    OUT.mkdir(parents=True, exist_ok=True)
    row0, col0 = pick_window()
    print(f"[1/6] ventana {WINDOW}² en (fila {row0}, col {col0})")

    with rasterio.open(MDT02) as ds:
        win = Window(col0, row0, WINDOW, WINDOW)
        z_ref = ds.read(1, window=win).astype(np.float32)
        transform = ds.window_transform(win)
        crs = ds.crs
        nodata = ds.nodata
    assert not np.any(z_ref == nodata), "la ventana pisa nodata: elegir otra"
    ref_tif = save_tif(OUT / "ref.tif", z_ref, transform, crs)

    print("[2/6] par sintético: degradar a 30 m + bicúbica de vuelta a 2 m")
    z30, t30 = degrade_to_coarse(z_ref, transform, crs)
    z30_tif = save_tif(OUT / "z30.tif", z30, t30, crs)
    z0 = resample_to_fine(z30, t30, crs, z_ref.shape, transform,
                          Resampling.cubic)
    bic_tif = save_tif(OUT / "bicubica.tif", z0, transform, crs)

    print("[3/6] red del DEM grueso (constraint) + uplift invertido")
    net30 = extract_network(z30_tif, OUT, "c30", COARSE := 30.0,
                            threshold_km2=THRESHOLD_KM2)
    with rasterio.open(net30["streams"]) as ds:
        s = ds.read(1)
        streams30 = (s != (ds.nodata if ds.nodata is not None else 0)) & (s > 0)
    with rasterio.open(net30["pointer"]) as ds:
        pointer30 = ds.read(1)
    with rasterio.open(net30["facc"]) as ds:
        facc30 = ds.read(1)
    with rasterio.open(net30["filled"]) as ds:
        filled30 = ds.read(1)
    network = rasterize_network(streams30, pointer30, z_ref.shape, z0)
    u30 = build_uplift(filled30, facc30, m_exp=0.45)
    u_fine = resample_to_fine(u30, t30, crs, z_ref.shape, transform,
                              Resampling.bilinear)
    print(f"      red gruesa: {int(streams30.sum())} celdas @30m -> "
          f"{int(network.sum())} celdas quemadas @2m")

    print("[4/6] LEM fino (Braun-Willett numpy)...")
    t_lem = time.time()
    z_v0 = amplify_v0(z0, network, u_fine)
    v0_tif = save_tif(OUT / "v0.tif", z_v0, transform, crs)
    print(f"      LEM: {time.time() - t_lem:.1f} s")

    print("[5/6] validación (pipeline E0, 1 hilo)...")
    net_ref = extract_network(ref_tif, OUT, "fref", FINE_CELL, THRESHOLD_KM2)
    net_bic = extract_network(bic_tif, OUT, "fbic", FINE_CELL, THRESHOLD_KM2)
    net_v0 = extract_network(v0_tif, OUT, "fv0", FINE_CELL, THRESHOLD_KM2)

    mask = np.ones(z_ref.shape, bool)
    res = {"ventana": {"fila": row0, "col": col0, "lado": WINDOW},
           "umbral_km2": THRESHOLD_KM2,
           "bicubica": {
               "elevacion": elevation_metrics(z_ref, z0, mask),
               "derivados": derivative_metrics(z_ref, z0, mask, FINE_CELL),
               "red": compare_networks(net_ref["streams"], net_bic["streams"],
                                       FINE_CELL)},
           "v0": {
               "elevacion": elevation_metrics(z_ref, z_v0, mask),
               "derivados": derivative_metrics(z_ref, z_v0, mask, FINE_CELL),
               "red": compare_networks(net_ref["streams"], net_v0["streams"],
                                       FINE_CELL)}}

    print("[6/6] resumen")
    for name in ("bicubica", "v0"):
        e = res[name]["elevacion"]
        d = res[name]["derivados"]
        r = res[name]["red"]
        print(f"  {name:9s} RMSE {e['RMSE_m']:.2f} m  NMAD {e['NMAD_m']:.2f} m"
              f"  slopeME {d['slope_ME_deg']:+.2f}°"
              f"  F1@10m {r['tolerancias']['10 m']['F1']:.4f}"
              f"  F1@30m {r['tolerancias']['30 m']['F1']:.4f}")
    res["duracion_s"] = round(time.time() - t0, 1)
    (OUT / "resultados-t1b.json").write_text(
        json.dumps(res, indent=2, ensure_ascii=False), encoding="utf-8")
    f1_bic = res["bicubica"]["red"]["tolerancias"]["10 m"]["F1"]
    f1_v0 = res["v0"]["red"]["tolerancias"]["10 m"]["F1"]
    verdict = "SUPERADO" if f1_v0 > f1_bic else "NO superado"
    print(f"\nT1b: F1@10m v0 {f1_v0:.4f} vs bicubica {f1_bic:.4f} -> {verdict}")


if __name__ == "__main__":
    main()
