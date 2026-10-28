"""Descarga las texturas CC0 de Poly Haven del terreno (albedo + normal GL).

Uso (desde la raiz del repo):  uv run python tools/fetch_textures.py [--res 1k|2k]

Escribe assets/textures/terrain/<material>_{albedo,normal}.jpg y
assets/textures/terrain/manifest.json con el tamano fisico (m) de cada
textura, que el render usa para el tiling a escala real. Idempotente: si el
fichero existe y su md5 coincide, no lo vuelve a bajar. Los assets no se
versionan (ver .gitignore); este script es la fuente de verdad.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import urllib.request
from pathlib import Path

API = "https://api.polyhaven.com"
OUT_DIR = Path("assets/textures/terrain")
USER_AGENT = "ProceduralTerrain-fetch/1.0"

# Material del generador (terrain::Material, en minusculas) -> asset de Poly Haven.
MATERIAL_ASSETS: dict[str, str] = {
    "seabed": "coast_sand_05",
    "sand": "coast_sand_01",
    "grass": "sparse_grass",
    "forest": "forest_ground_04",
    "rock": "rock_face_03",
    "snow": "snow_02",
    "riverbed": "dry_riverbed_rock",
    "lakebed": "brown_mud_03",
    "gravel": "ganges_river_pebbles",
    "marsh": "brown_mud_02",
}

MAPS = {"albedo": "Diffuse", "normal": "nor_gl"}


def get_json(url: str) -> dict:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as resp:
        return json.loads(resp.read().decode("utf-8"))


def md5_of(path: Path) -> str:
    return hashlib.md5(path.read_bytes()).hexdigest()


def download(url: str, dest: Path, md5: str) -> None:
    if dest.exists() and md5_of(dest) == md5:
        return
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=300) as resp:
        data = resp.read()
    if hashlib.md5(data).hexdigest() != md5:
        raise RuntimeError(f"md5 no coincide para {url}")
    dest.write_bytes(data)


def fetch_material(material: str, asset: str, res: str) -> dict:
    files = get_json(f"{API}/files/{asset}")
    info = get_json(f"{API}/info/{asset}")
    for suffix, key in MAPS.items():
        entry = files[key][res]["jpg"]
        download(entry["url"], OUT_DIR / f"{material}_{suffix}.jpg", entry["md5"])
    # dimensions viene en milimetros: [ancho, alto]
    size_m = float(info["dimensions"][0]) / 1000.0
    return {"asset": asset, "size_m": size_m, "author": ", ".join(info["authors"])}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--res", default="1k", choices=["1k", "2k"])
    args = parser.parse_args()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    manifest: dict[str, dict] = {}
    for material, asset in MATERIAL_ASSETS.items():
        print(f"{material:9s} <- {asset}")
        manifest[material] = fetch_material(material, asset, args.res)
    manifest_path = OUT_DIR / "manifest.json"
    manifest_path.write_text(json.dumps(
        {"source": "Poly Haven (CC0)", "res": args.res, "materials": manifest},
        indent=2), encoding="utf-8")
    print(f"ok: {len(manifest)} materiales en {OUT_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
