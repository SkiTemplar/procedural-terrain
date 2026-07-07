"""Amplificador v0 (T1b del plan maestro, Apéndice C.4.1).

Pipeline: bicúbica como base → red de drenaje del DEM GRUESO quemada como
constraint → uplift de equilibrio invertido de la SPL (U ∝ (A/A0)^m · S) →
LEM fino Braun-Willett hasta relajar. El detalle emerge del proceso; la red
y la macro-forma vienen de los datos.
"""

from pathlib import Path

import numpy as np
import rasterio
from rasterio.transform import Affine
from rasterio.warp import Resampling, reproject

from amplify.lem import (accumulate_area, d8_receivers, diffuse,
                         implicit_spl_step, level_order, topo_levels)

FINE_CELL = 2.0
COARSE_CELL = 30.0
# Área crítica de canalización (Flint), como en terrain/lem.cpp: por debajo
# la celda es LADERA (equilibrio S = pendiente observada), no canal SPL. Con
# un umbral pequeño (900 m² = 1 celda gruesa) TODA ladera fina se trataba
# como canal e intentaba empinarse (A30/A0)^m veces — relieve desbocado
# (+7° de pendiente media, RMSE 19-26 m en los intentos 1-2).
A0_M2 = 22500.0

# Codificación del puntero D8 de WhiteboxTools (horario desde NE).
POINTER_DIRS = {1: (1, -1), 2: (1, 0), 4: (1, 1), 8: (0, 1),
                16: (-1, 1), 32: (-1, 0), 64: (-1, -1), 128: (0, -1)}


def save_tif(path: Path, arr: np.ndarray, transform: Affine, crs,
             nodata: float | None = None) -> Path:
    profile = {
        "driver": "GTiff", "height": arr.shape[0], "width": arr.shape[1],
        "count": 1, "dtype": "float32", "transform": transform, "crs": crs,
        "compress": "deflate",
    }
    if nodata is not None:
        profile["nodata"] = nodata
    with rasterio.open(path, "w", **profile) as ds:
        ds.write(arr.astype(np.float32), 1)
    return path


def degrade_to_coarse(z_fine: np.ndarray, transform: Affine, crs) -> tuple:
    """DEM fino → malla de 30 m por promediado (el par sintético de E1)."""
    h = int(round(z_fine.shape[0] * FINE_CELL / COARSE_CELL))
    w = int(round(z_fine.shape[1] * FINE_CELL / COARSE_CELL))
    dst = np.zeros((h, w), np.float32)
    dst_transform = Affine(COARSE_CELL, 0, transform.c, 0, -COARSE_CELL, transform.f)
    reproject(z_fine, dst, src_transform=transform, src_crs=crs,
              dst_transform=dst_transform, dst_crs=crs,
              resampling=Resampling.average)
    return dst, dst_transform


def resample_to_fine(z30: np.ndarray, transform30: Affine, crs,
                     fine_shape: tuple, fine_transform: Affine,
                     method: Resampling) -> np.ndarray:
    dst = np.zeros(fine_shape, np.float32)
    reproject(z30, dst, src_transform=transform30, src_crs=crs,
              dst_transform=fine_transform, dst_crs=crs, resampling=method)
    return dst


def rasterize_network(streams30: np.ndarray, pointer30: np.ndarray,
                      fine_shape: tuple, z_fine: np.ndarray,
                      snap_radius: int = 7) -> np.ndarray:
    """Traza la red gruesa sobre la malla fina con VALLEY SNAPPING: cada nodo
    de la polilínea se ajusta al mínimo de la superficie bicúbica en un disco
    de ±snap_radius celdas finas. La malla de 30 m posiciona el cauce con
    ±1-2 celdas gruesas de error lateral; el fondo de valle interpolado (que
    deriva SOLO del DEM grueso — sin mirar la referencia) está mucho más
    cerca del cauce real. Sin snapping la constraint perdía contra la
    bicúbica en F1@10 m por pura colocación."""
    scale = COARSE_CELL / FINE_CELL
    h, w = fine_shape

    dy, dx = np.mgrid[-snap_radius:snap_radius + 1, -snap_radius:snap_radius + 1]
    disc = (dy * dy + dx * dx) <= snap_radius * snap_radius

    def snap(r: int, c: int) -> tuple[int, int]:
        y = int(round((r + 0.5) * scale))
        x = int(round((c + 0.5) * scale))
        y0, y1 = max(0, y - snap_radius), min(h, y + snap_radius + 1)
        x0, x1 = max(0, x - snap_radius), min(w, x + snap_radius + 1)
        window = z_fine[y0:y1, x0:x1].astype(np.float64).copy()
        d = disc[(y0 - y + snap_radius):(y1 - y + snap_radius),
                 (x0 - x + snap_radius):(x1 - x + snap_radius)]
        window[~d] = np.inf
        j = int(np.argmin(window))
        return y0 + j // window.shape[1], x0 + j % window.shape[1]

    stream_cells = set(zip(*np.nonzero(streams30)))
    snapped = {rc: snap(*rc) for rc in stream_cells}

    def snap_local(y: int, x: int, radius: int) -> tuple[int, int]:
        y0, y1 = max(0, y - radius), min(h, y + radius + 1)
        x0, x1 = max(0, x - radius), min(w, x + radius + 1)
        window = z_fine[y0:y1, x0:x1]
        j = int(np.argmin(window))
        return y0 + j // window.shape[1], x0 + j % window.shape[1]

    mask = np.zeros(fine_shape, bool)
    for (r, c), (y0, x0) in snapped.items():
        p = int(pointer30[r, c])
        dc, dr = POINTER_DIRS.get(p, (0, 0))
        nxt = (r + dr, c + dc)
        if nxt in snapped:
            y1, x1 = snapped[nxt]
        else:
            y1 = int(round((nxt[0] + 0.5) * scale))
            x1 = int(round((nxt[1] + 0.5) * scale))
        # Densificar el tramo: la recta entre nodos ataja los meandros del
        # valle; puntos intermedios cada ~4 celdas re-ajustados al mínimo
        # local siguen la curva del fondo.
        seg_len = max(abs(y1 - y0), abs(x1 - x0))
        n_mid = max(2, seg_len // 4)
        pts = [(y0, x0)]
        for t in np.linspace(0.0, 1.0, n_mid + 1)[1:-1]:
            my = int(round(y0 + (y1 - y0) * t))
            mx = int(round(x0 + (x1 - x0) * t))
            pts.append(snap_local(my, mx, 4))
        pts.append((y1, x1))
        for (ay, ax), (by, bx) in zip(pts[:-1], pts[1:]):
            steps = max(4, int(3 * max(abs(by - ay), abs(bx - ax))))
            ys = np.clip(np.round(np.linspace(ay, by, steps)).astype(int), 0, h - 1)
            xs = np.clip(np.round(np.linspace(ax, bx, steps)).astype(int), 0, w - 1)
            mask[ys, xs] = True
    return mask


def build_uplift(filled30: np.ndarray, facc30_cells: np.ndarray,
                 m_exp: float) -> np.ndarray:
    """Inversión de la SPL en equilibrio: U = (A/A0)^m · S con A y S del DEM
    grueso, SIN normalizar — así el estado estacionario del LEM fino es
    S_eq = S30 · (A30/A_fino)^m: reproduce las pendientes reales donde el
    drenaje fino coincide con el grueso y las amplifica donde difiere (el
    detalle sub-celda emerge de esa diferencia, no de ruido)."""
    a_m2 = facc30_cells * COARSE_CELL * COARSE_CELL
    gy, gx = np.gradient(filled30.astype(np.float64), COARSE_CELL)
    slope = np.hypot(gx, gy)
    u = np.power(np.maximum(a_m2, A0_M2) / A0_M2, m_exp) * slope
    return u.astype(np.float32)


def amplify_v0(z0: np.ndarray, network: np.ndarray, uplift_fine: np.ndarray,
               iterations: int = 25, k_dt: float = 1.0, m_exp: float = 0.45,
               burn_m: float = 1.5, diffusion: float = 0.0,
               a0_m2: float = A0_M2) -> np.ndarray:
    """LEM fino con la red quemada. Devuelve el DEM amplificado."""
    z = z0.copy()
    z[network] -= np.float32(burn_m)

    # La red observada es condición de contorno (Dirichlet), no celda
    # erosionable: si la trinchera quemada quedara como pit, su propio uplift
    # de canal la rellenaría en pocas iteraciones (fallo del intento 1).
    dirichlet = network.copy()
    dirichlet[0, :] = dirichlet[-1, :] = True
    dirichlet[:, 0] = dirichlet[:, -1] = True

    du_full = (np.float32(k_dt) * uplift_fine).astype(np.float64).ravel()
    cell_area = FINE_CELL * FINE_CELL
    for _ in range(iterations):
        rcv, dist = d8_receivers(z, FINE_CELL)
        lvl = topo_levels(rcv)
        order, starts = level_order(lvl)
        area = accumulate_area(rcv, order, starts, cell_area)
        # Mínima intervención: las LADERAS (A < A0 de Flint) no se tocan — su
        # curvatura bicúbica es información real que enruta los tributarios
        # (relajarlas a facetas S30 hundió el F1 en el intento 3). Solo los
        # CANALES finos evolucionan, anclados a la red quemada (Dirichlet).
        hillslope = area < a0_m2
        f = (k_dt * np.power(np.maximum(area, a0_m2) / a0_m2, m_exp)
             / dist.astype(np.float64))
        f[hillslope] = 0.0
        du = du_full.copy()
        du[hillslope] = 0.0
        z = implicit_spl_step(z, rcv, dist, order, starts, f, du, dirichlet)
        if diffusion > 0.0:
            z = diffuse(z, diffusion)
    return z
