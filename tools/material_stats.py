"""Histograma exacto de materiales de orilla en el BMP de dump (colores de paleta)."""

import sys

import numpy as np

path = sys.argv[1]
with open(path, "rb") as f:
    data = f.read()
w = int.from_bytes(data[18:22], "little")
h = int.from_bytes(data[22:26], "little")
off = int.from_bytes(data[10:14], "little")
row = (w * 3 + 3) & ~3
img = np.zeros((h, w, 3), np.uint8)
for y in range(h):
    r = np.frombuffer(data, np.uint8, w * 3, off + (h - 1 - y) * row)
    img[y] = r.reshape(w, 3)[:, ::-1]  # BGR->RGB

palette = {
    "arena": (219, 201, 148),   # 0.86 0.79 0.58
    "grava": (148, 140, 127),   # 0.58 0.55 0.50
    "marisma": (114, 125, 76),  # 0.45 0.49 0.30
    "roca": (125, 117, 110),    # 0.49 0.46 0.43
}
flat = img.reshape(-1, 3)
for name, rgb in palette.items():
    exact = np.all(np.abs(flat.astype(int) - np.array(rgb)) <= 1, axis=1).sum()
    print(f"{name:8s}: {exact} celdas")
