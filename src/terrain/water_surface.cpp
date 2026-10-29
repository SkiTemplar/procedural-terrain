#include "terrain/water_surface.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace terrain {

namespace {

constexpr float kBankDropM = 0.15f; // la orilla seca entierra el vértice
constexpr float kSinkFadeM = 0.5f;  // sobre el mar, el hundido crece de 0 a pleno

} // namespace

WaterSurface compute_water_surface(const TerrainData& data) {
    const auto& hf = data.height;
    const auto& wl = data.hydrology.water_level;
    const int res = hf.resolution();
    const float sea = data.config.sea_level;
    const auto sample = [res](int x, int z) {
        return static_cast<std::size_t>(z) * static_cast<std::size_t>(res)
             + static_cast<std::size_t>(x);
    };

    WaterSurface s;
    s.res = res;
    s.vertex_y.assign(static_cast<std::size_t>(res) * static_cast<std::size_t>(res),
                      std::numeric_limits<float>::quiet_NaN());
    s.quad_covered.assign(static_cast<std::size_t>(res - 1)
                              * static_cast<std::size_t>(res - 1),
                          0);
    if (wl.size() != s.vertex_y.size() || res < 2) {
        return s;
    }

    // Altura de un vértice la primera vez que lo pide un quad (los vértices
    // se comparten: manda el primer quad en orden de fila, como en la malla).
    const auto vertex_height = [&](int x, int z, float fallback) {
        const float own = wl[sample(x, z)];
        const float h = hf.at(x, z);
        if (std::isnan(own)) {
            // Orilla seca: el vértice se ENTIERRA bajo el talud (la línea de
            // agua la dibuja la intersección z-buffer), pero nunca baja del
            // mar: donde el lecho está sumergido la cinta muere a cota de mar
            // en vez de bajar al fondo (con el mar recortado, ese hundido
            // dejaba el lecho al aire dentro del estuario).
            return std::max(std::min(fallback, h - kBankDropM) - kWaterSinkM, sea);
        }
        // En el remanso (lámina a cota de mar) no se hunde: la cinta enrasa
        // con el plano del mar en la boca, sin escalón de 12 cm.
        const float sink =
            kWaterSinkM * std::clamp((own - sea) / kSinkFadeM, 0.0f, 1.0f);
        return own - sink;
    };

    for (int z = 0; z < res - 1; ++z) {
        for (int x = 0; x < res - 1; ++x) {
            // Fallback = MÍNIMO de los niveles definidos: con el máximo la
            // lámina se estiraba hacia arriba sobre las orillas.
            float level = std::numeric_limits<float>::max();
            bool any = false;
            for (const auto [dx, dz] : {std::pair{0, 0}, {1, 0}, {0, 1}, {1, 1}}) {
                const float v = wl[sample(x + dx, z + dz)];
                if (!std::isnan(v)) {
                    level = std::min(level, v);
                    any = true;
                }
            }
            if (!any) {
                continue;
            }
            s.quad_covered[s.quad_index(x, z)] = 1;
            for (const auto [dx, dz] : {std::pair{0, 0}, {1, 0}, {0, 1}, {1, 1}}) {
                float& y = s.vertex_y[sample(x + dx, z + dz)];
                if (std::isnan(y)) {
                    y = vertex_height(x + dx, z + dz, level);
                }
            }
        }
    }
    return s;
}

} // namespace terrain
