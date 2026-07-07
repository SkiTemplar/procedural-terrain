"""Braun-Willett O(n) implícito en numpy puro — port de `src/terrain/lem.cpp`.

Sin bucles Python por celda: el orden topológico se obtiene como profundidad
al outlet por *pointer doubling* (O(log profundidad) pasadas vectorizadas) y
tanto la acumulación de área como el solver implícito se procesan POR NIVELES
(cada nodo depende solo de su receptor, que vive en un nivel anterior).
"""

import numpy as np

# Vecindario D8: (dx, dz) y distancia relativa.
OFFSETS = [(-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1), (1, 1)]


def d8_receivers(z: np.ndarray, cell: float) -> tuple[np.ndarray, np.ndarray]:
    """Receptor D8 (índice plano; self si no hay bajada) y distancia al mismo."""
    h, w = z.shape
    zp = np.pad(z, 1, constant_values=np.inf)
    best_slope = np.zeros((h, w), np.float32)
    base = np.arange(h * w, dtype=np.int64).reshape(h, w)
    rcv = base.copy()
    dist = np.full((h, w), cell, np.float32)
    for dx, dz in OFFSETS:
        nb = zp[1 + dz:1 + dz + h, 1 + dx:1 + dx + w]
        d = cell * 1.4142135 if (dx != 0 and dz != 0) else cell
        slope = (z - nb) / np.float32(d)
        better = slope > best_slope
        best_slope = np.where(better, slope, best_slope)
        rcv = np.where(better, base + dz * w + dx, rcv)
        dist = np.where(better, np.float32(d), dist)
    return rcv.ravel(), dist.ravel()


def topo_levels(rcv: np.ndarray) -> np.ndarray:
    """Profundidad de cada nodo hasta su raíz (pointer doubling)."""
    n = rcv.size
    lvl = (rcv != np.arange(n, dtype=rcv.dtype)).astype(np.int64)
    p = rcv.copy()
    while True:
        nxt = p[p]
        if np.array_equal(nxt, p):
            break
        lvl = lvl + lvl[p]
        p = nxt
    return lvl


def level_order(lvl: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Orden por nivel + offsets de cada nivel (CSR sobre el sort estable)."""
    order = np.argsort(lvl, kind="stable")
    counts = np.bincount(lvl)
    starts = np.concatenate(([0], np.cumsum(counts)))
    return order, starts


def accumulate_area(rcv: np.ndarray, order: np.ndarray, starts: np.ndarray,
                    cell_area: float) -> np.ndarray:
    """Área drenada por celda: se suma de donantes a receptores (niveles
    descendentes; los receptores de un nivel viven en niveles anteriores)."""
    area = np.full(rcv.size, cell_area, np.float64)
    for k in range(len(starts) - 2, 0, -1):
        nodes = order[starts[k]:starts[k + 1]]
        np.add.at(area, rcv[nodes], area[nodes])
    return area


def implicit_spl_step(z: np.ndarray, rcv: np.ndarray, dist: np.ndarray,
                      order: np.ndarray, starts: np.ndarray, f: np.ndarray,
                      du: np.ndarray, dirichlet: np.ndarray) -> np.ndarray:
    """Un paso implícito de stream power (n=1): h=(h+du+f·h_rcv)/(1+f),
    resuelto de receptores a donantes. `dirichlet` fija celdas (borde, mar)."""
    hf = z.ravel().astype(np.float64)
    du = du.ravel()
    fixed = dirichlet.ravel()
    for k in range(1, len(starts) - 1):
        nodes = order[starts[k]:starts[k + 1]]
        nodes = nodes[~fixed[nodes]]
        if nodes.size == 0:
            continue
        fr = f[nodes]
        hf[nodes] = (hf[nodes] + du[nodes] + fr * hf[rcv[nodes]]) / (1.0 + fr)
    # Nivel 0 = raíces: pits interiores suben con el uplift (se rellenan solos),
    # las celdas Dirichlet no se tocan.
    roots = order[starts[0]:starts[1]]
    roots = roots[~fixed[roots]]
    hf[roots] += du[roots]
    return hf.reshape(z.shape).astype(np.float32)


def diffuse(z: np.ndarray, k: float) -> np.ndarray:
    """Difusión de laderas (laplaciano 5 puntos), borde intacto."""
    out = z.copy()
    lap = (z[:-2, 1:-1] + z[2:, 1:-1] + z[1:-1, :-2] + z[1:-1, 2:]
           - 4.0 * z[1:-1, 1:-1])
    out[1:-1, 1:-1] += np.float32(k) * lap
    return out
