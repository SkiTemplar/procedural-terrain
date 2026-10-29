#pragma once

#include "terrain/heightfield.h"
#include "terrain/terrain_config.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace terrain {

// Presets JSON (C1 del plan): un preset es el TerrainConfig COMPLETO
// (incluida la seed) más el hash del heightfield que produjo al guardarse —
// cargar y regenerar debe reproducir exactamente el mismo mundo (el test
// Preset.CargarYRegenerarReproduceElMundo lo protege). La serialización es
// campo a campo y explícita: un campo nuevo del config debe añadirse aquí
// a conciencia (y romperá el round-trip del test si se olvida a medias).

// Versión de la semántica del clima en el preset. v1 (sin campo): la nieve
// era min(snow_line_m, 0.60·cumbre) y el bosque llegaba a nieve − 150 m.
// v2: snow_line_m es la cota absoluta de la nieve estacional con exposición
// neutra y treeline_m la isoterma de 10 °C (ecología física). La conversión
// v1 → v2 no es automática (la cumbre solo se conoce tras generar): un preset
// v1 carga tal cual y el llamante avisa de que su nieve y su bosque cambian.
inline constexpr int kPresetClimateVersion = 2;

struct Preset {
    TerrainConfig config;
    std::uint64_t height_hash = 0; // 0 = preset sin hash (aún no generado)
    int climate_version = 1;       // 1 = preset anterior a la ecología física
};

// FNV-1a de 64 bits sobre los bits de los floats del heightfield: barato,
// determinista y suficiente para detectar cualquier divergencia de mundo.
std::uint64_t heightfield_hash(const Heightfield& hf);

nlohmann::json preset_to_json(const TerrainConfig& cfg);

// Escritura/lectura con manejo de errores explícito: false/nullopt en fallo
// de E/S o JSON inválido — nunca excepciones hacia el llamante.
bool save_preset(const std::string& path, const TerrainConfig& cfg,
                 std::uint64_t height_hash);
std::optional<Preset> load_preset(const std::string& path);

// Nombres de fichero .json del directorio de presets (sin extensión),
// ordenados; vacío si el directorio no existe.
std::vector<std::string> list_presets(const std::string& dir);

} // namespace terrain
