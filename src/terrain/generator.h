#pragma once

#include "terrain/heightfield.h"
#include "terrain/hydrology.h"
#include "terrain/terrain_config.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace terrain {

enum class Material : std::uint8_t {
    Seabed,
    Sand,
    Grass,
    Forest,
    Rock,
    Snow,
    RiverBed,
    LakeBed,
    Gravel, // orilla expuesta al oleaje con pendiente: playa de grava/cantos
    Marsh,  // orilla abrigada con aporte fluvial: marisma
};

// Nº de valores del enum — para histogramas indexados por material.
inline constexpr std::size_t kMaterialCount =
    static_cast<std::size_t>(Material::Marsh) + 1;

struct TerrainData {
    Heightfield height{2, 1.0f};
    Hydrology hydrology;
    std::vector<Material> material;      // por muestra del heightfield
    std::vector<float> forest_density;   // [0,1] gradiente de bosque
    std::vector<float> climate;          // [0,1] 0=frío (Escocia) 1=cálido (Caribe)
    std::vector<float> wave_energy;      // [0,1] REI/fetch en la banda litoral
    TerrainConfig config;
};

// Pipeline completo: continentalidad → relieve → erosión → hidrología → materiales.
// `progress` (opcional) se invoca al entrar en cada fase con un literal
// estático — pensado para informar a la UI desde un hilo de trabajo.
TerrainData generate(const TerrainConfig& cfg,
                     const std::function<void(const char*)>& progress = {});

} // namespace terrain
