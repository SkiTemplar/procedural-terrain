"""E0 — verificación de los datos descargados: CRS, malla, cobertura y solape."""

import rasterio
from rasterio.warp import transform_bounds

MDT02 = "e0/data/MDT02-ETRS89-HU31-0180-1-COB2.tif"
GLO30 = "e0/data/GLO30_N42_E000.tif"


def describe(path: str) -> None:
    with rasterio.open(path) as ds:
        print(f"--- {path}")
        print(f"  crs: {ds.crs}")
        print(f"  size: {ds.width} x {ds.height}  res: {ds.res}")
        print(f"  bounds: {ds.bounds}")
        print(f"  dtype: {ds.dtypes[0]}  nodata: {ds.nodata}")
        bounds84 = transform_bounds(ds.crs, "EPSG:4326", *ds.bounds)
        print(f"  bounds WGS84 (lon/lat): {tuple(round(v, 4) for v in bounds84)}")


describe(MDT02)
describe(GLO30)
