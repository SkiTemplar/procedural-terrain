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

struct Preset {
    TerrainConfig config;
    std::uint64_t height_hash = 0; // 0 = preset sin hash (aún no generado)
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
