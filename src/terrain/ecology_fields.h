#pragma once

// Utilidades de campos escalares cuadrados (res²) compartidas por la ecología
// y su informe. Uso interno de terrain_gen.

#include <vector>

namespace terrain::eco_detail {

// Box blur separable de radio r (in situ, clamp en el borde).
void box_blur(std::vector<float>& f, int res, int r, int passes);

// Interpolación bilineal en coordenadas de grid (clamp al borde).
[[nodiscard]] float bilinear(const std::vector<float>& grid, int gres, float fx, float fz);

[[nodiscard]] float smooth01(float e0, float e1, float x) noexcept;

} // namespace terrain::eco_detail
