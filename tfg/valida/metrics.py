"""E0 — métricas de elevación y de derivados del terreno (informe §5).

Elevación: ME (sesgo), RMSE, RMSE centrado, MAE, mediana, NMAD (Höhle & Höhle
2009), LE90/LE95. Derivados: RMSE de pendiente y de aspecto (circular, solo
donde la pendiente de referencia supera un umbral).
"""

import numpy as np

ASPECT_SLOPE_MIN_DEG = 5.0


def elevation_metrics(ref: np.ndarray, pred: np.ndarray, mask: np.ndarray) -> dict:
    d = (pred[mask] - ref[mask]).astype(np.float64)
    abs_d = np.abs(d)
    med = float(np.median(d))
    return {
        "n_celdas": int(d.size),
        "ME_sesgo_m": float(d.mean()),
        "mediana_m": med,
        "RMSE_m": float(np.sqrt(np.mean(d**2))),
        "RMSE_centrado_m": float(np.sqrt(np.mean((d - d.mean()) ** 2))),
        "MAE_m": float(abs_d.mean()),
        "NMAD_m": float(1.4826 * np.median(np.abs(d - med))),
        "LE90_m": float(np.percentile(abs_d, 90)),
        "LE95_m": float(np.percentile(abs_d, 95)),
    }


def slope_aspect(z: np.ndarray, cell: float) -> tuple[np.ndarray, np.ndarray]:
    """Pendiente (grados) y aspecto (grados desde el norte, horario) por
    diferencias centradas — mismo operador para referencia y predicción."""
    dz_dy, dz_dx = np.gradient(z.astype(np.float64), cell)
    slope = np.degrees(np.arctan(np.hypot(dz_dx, dz_dy)))
    aspect = (np.degrees(np.arctan2(-dz_dx, dz_dy)) + 180.0) % 360.0
    return slope.astype(np.float32), aspect.astype(np.float32)


def derivative_metrics(
    ref: np.ndarray, pred: np.ndarray, mask: np.ndarray, cell: float
) -> dict:
    slope_ref, aspect_ref = slope_aspect(ref, cell)
    slope_pred, aspect_pred = slope_aspect(pred, cell)

    # el gradiente en el borde de la máscara mezcla nodata: erosionar 1 celda
    interior = mask.copy()
    interior[:1, :] = interior[-1:, :] = False
    interior[:, :1] = interior[:, -1:] = False

    ds = (slope_pred[interior] - slope_ref[interior]).astype(np.float64)
    out = {
        "slope_RMSE_deg": float(np.sqrt(np.mean(ds**2))),
        "slope_ME_deg": float(ds.mean()),
        "slope_media_ref_deg": float(slope_ref[interior].mean()),
        "slope_media_pred_deg": float(slope_pred[interior].mean()),
    }

    steep = interior & (slope_ref > ASPECT_SLOPE_MIN_DEG)
    da = np.abs(aspect_pred[steep] - aspect_ref[steep]).astype(np.float64)
    da = np.minimum(da, 360.0 - da)
    out["aspect_RMSE_deg"] = float(np.sqrt(np.mean(da**2)))
    out["aspect_umbral_slope_deg"] = ASPECT_SLOPE_MIN_DEG
    return out
