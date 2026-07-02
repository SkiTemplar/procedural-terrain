#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace terrain {

// Placas tectónicas simplificadas: Voronoi de ~14 placas con vectores de
// deriva. El tipo de interacción en cada borde produce el relieve:
//   convergente  → uplift en banda (cordilleras que siguen el borde)
//   divergente   → rift (fosa hundida)
//   transformante→ falla (zanja estrecha, visible como línea en el terreno)
// Determinista por seed y muestreable por posición: migra a la esfera tal cual.
class Tectonics {
public:
    Tectonics(std::uint32_t seed, float domain_size_m, int plate_count = 14);

    struct Sample {
        float uplift = 0.0f; // [0,1] banda de cordillera
        float rift = 0.0f;   // [0,1] fosa divergente
        float fault = 0.0f;  // [0,1] zanja transformante
        float bias = 0.0f;   // [-1,1] carácter de la placa (oceánica/continental)
    };

    Sample sample(glm::vec2 pos_m) const;

private:
    struct Plate {
        glm::vec2 site;
        glm::vec2 drift; // unitario
        float bias;      // [-1,1]
    };

    std::vector<Plate> plates_;
};

} // namespace terrain
