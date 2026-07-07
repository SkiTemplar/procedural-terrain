"""Media de energía de oleaje por octante de brújula (desde el centroide)."""

import sys

import matplotlib.pyplot as plt
import numpy as np

img = plt.imread(sys.argv[1])[:, :, :3]  # [0,1]
h, w, _ = img.shape

r, g, b = img[..., 0], img[..., 1], img[..., 2]
sea = (b > 0.25) & (r < 0.15)  # azul marino del dump
gray = (np.abs(r - g) < 0.03) & (np.abs(g - b) < 0.03)
rim = gray & (r > 0.02) & ~sea  # banda litoral con energía

ys, xs = np.nonzero(rim)
e = r[ys, xs]
cy, cx = ys.mean(), xs.mean()
# ángulo brújula: 0=N (arriba, -y), 90=E (derecha, +x)
ang = (np.degrees(np.arctan2(xs - cx, -(ys - cy)))) % 360
octant = ((ang + 22.5) // 45).astype(int) % 8
names = ["N", "NE", "E", "SE", "S", "SO", "O", "NO"]
print(f"celdas de banda con E>0: {len(e)}   E media global: {e.mean():.3f}")
for k in range(8):
    m = octant == k
    print(f"  {names[k]:2s}: E media {e[m].mean():.3f}  (n={m.sum()})")
