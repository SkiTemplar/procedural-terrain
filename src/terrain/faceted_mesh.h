#pragma once

#include "render/mesh.h"
#include "terrain/detail.h"
#include "terrain/generator.h"
#include "terrain/palette.h"

#include <array>

namespace terrain {

// Trozo rectangular del heightfield que se mallará en facetas: celda inicial,
// celdas que abarca, subdivisión de la celda (nodos por celda: 2 = facetas de
// ~1.2 m junto al jugador, con el relieve fino del DetailSampler) y paso de
// rejilla en subceldas (subceldas por faceta).
struct FacetRegion {
    int x0 = 0;
    int z0 = 0;
    int cells_x = 0;
    int cells_z = 0;
    int stride = 1;
    int sub = 1;
    // Tamaño de faceta (celdas) del vecino de cada borde — z0, z1, x0, x1 —
    // cuando es más basto que el propio; 0 = igual o más fino. En esos bordes
    // los nodos intermedios se alinean con la arista del vecino y los nodos
    // compartidos se filtran como él: costura sin escalón.
    std::array<float, 4> edge_cells{0.0f, 0.0f, 0.0f, 0.0f};
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
