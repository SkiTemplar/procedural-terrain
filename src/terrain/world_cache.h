#pragma once

#include "terrain/generator.h"

#include <optional>
#include <string>

namespace terrain {

// Caché binaria de un mundo ya generado (TerrainData completo) para los
// modos offscreen: generar 4096² cuesta ~2 min y las iteraciones de render no
// tocan el generador. El fichero guarda el config serializado como preset
// JSON; load_world_cache solo acepta la caché si coincide con el config
// pedido. NO detecta cambios en el CÓDIGO del generador: quien lo toque debe
// borrar la caché (el llamante la nombra explícitamente con --cache).
//
// Serialización campo a campo y explícita, como los presets: un campo nuevo
// de TerrainData debe añadirse aquí a conciencia (el test de ida y vuelta
// compara todos los campos que el render consume).
bool save_world_cache(const std::string& path, const TerrainData& data);

// nullopt si el fichero no existe, está corrupto, es de otra versión o su
// config no coincide con `expected`. Nunca lanza.
std::optional<TerrainData> load_world_cache(const std::string& path,
                                            const TerrainConfig& expected);

} // namespace terrain
