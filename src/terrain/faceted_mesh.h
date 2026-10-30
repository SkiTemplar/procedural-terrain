#pragma once

#include "render/mesh.h"
#include "terrain/detail.h"
#include "terrain/generator.h"
#include "terrain/palette.h"

namespace terrain {

// Trozo rectangular del heightfield que se mallará en facetas: celda inicial,
// celdas que abarca y paso de rejilla (celdas por faceta).
struct FacetRegion {
    int x0 = 0;
    int z0 = 0;
    int cells_x = 0;
    int cells_z = 0;
    int stride = 1;
};

// Malla low-poly facetada (dirección de arte 2026-10): triángulos SIN vértices
// compartidos, cada uno con su normal de cara y un color propio evaluado en su
// centroide con los campos físicos (material, bosque, clima, lluvia, sequedad,
// litología, pendiente y orientación al sol). Los nodos se desplazan en XZ
// con un hash de su coordenada GLOBAL (vecinos del mismo nivel coinciden) y la
// diagonal de cada quad se elige por hash: sin retícula visible. La altura de
// cada nodo es la del DetailSampler (la superficie que pisa el jugador).
// Lleva faldón perimetral para tapar las grietas entre niveles de LOD.
render::Mesh build_faceted_mesh(const TerrainData& data, const DetailSampler& sampler,
                                const Palette& palette, const FacetRegion& region);

} // namespace terrain
