// Hashes golden del mundo: cualquier cambio de generación (intencionado o no)
// cambia el hash y rompe este test. Si el cambio es intencionado, se
// actualiza la constante EN EL MISMO COMMIT y se justifica en el mensaje.
// Sin esto, "misma seed ⇒ mismo mundo" solo se comprobaba dentro de una
// misma ejecución (DeterministaPorSeed), no entre versiones del código.

#include "terrain/generator.h"
#include "terrain/preset.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

std::uint64_t material_hash(const std::vector<terrain::Material>& m) {
    std::uint64_t h = 1469598103934665603ull; // FNV-1a, como heightfield_hash
    for (const terrain::Material v : m) {
        h ^= static_cast<std::uint64_t>(v);
        h *= 1099511628211ull;
    }
    return h;
}

// Mundo a escala real (10 km) con macro 128: misma física que el juego,
// coste de test razonable. Cubre LEM, hidrología, costa, materiales.
terrain::TerrainConfig golden_config() {
    terrain::TerrainConfig cfg;
    cfg.seed = 20260709;
    cfg.resolution = 512;
    cfg.size_m = 10000.0f;
    cfg.erosion_droplets = 20000;
    return cfg;
}

// Recalibrado 2026-10-30: relieve tectónico continuo + LEM con cabeceras de
// (40 m)² (red dendrítica); cambio intencionado del mundo. El material se
// recalibró de nuevo al bajar el sol canónico de 40° a 30° (la ecología usa su
// radiación y la sombra horneada) y al subir la perturbación de microclima de
// la ecología (kJitterC 0.3 → 0.8 °C); la altura no cambia.
// Recalibrado otra vez (altura y material): cabeceras del LEM de (40 m)² a
// (63 m)² y difusión de laderas 0.17 → 0.21 (menos barrancos de primer orden
// en peine, más confluencia).
// Recalibrado (altura y material): divisoria continua (suelo de culminación
// 0.30 → 0.50, warp del eje 1.4 → 1.9 km) y relieve inicial correlacionado
// del LEM (±12 m a ~1 km y ~350 m). Cumbre de la seed 20260711: 1289 m.
// Recalibrado (altura y material, iter. 9): diaclasado fino de ~60 m en la
// erodibilidad del LEM (barrancos de primer orden sin peine). Cumbre de la
// seed 20260711: 1370 m.
constexpr std::uint64_t kGoldenHeight = 0x11ae7c06a7e93272ull;
constexpr std::uint64_t kGoldenMaterial = 0x401d6afdfb57c0b3ull;

} // namespace

TEST(Golden, ElMundoDeReferenciaNoCambiaSinQuererlo) {
    const auto data = terrain::generate(golden_config());
    const std::uint64_t hh = terrain::heightfield_hash(data.height);
    const std::uint64_t hm = material_hash(data.material);
    std::printf("  golden: altura 0x%016llxull material 0x%016llxull\n",
                static_cast<unsigned long long>(hh),
                static_cast<unsigned long long>(hm));
    EXPECT_EQ(hh, kGoldenHeight);
    EXPECT_EQ(hm, kGoldenMaterial);
}
