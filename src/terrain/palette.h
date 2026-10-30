#pragma once

#include "terrain/generator.h"

#include <cstdint>

namespace terrain {

// Paleta del suelo (C4): color final por material con gradientes anclados a
// los campos del mundo generado — nivel del mar, línea de nieve efectiva,
// cumbre real y litología del LEM — nunca a cotas absolutas (review paleta
// 2026-07-11: las cotas fijas se degradaban al mover seed o sliders).
// Compartida por el terreno chunked y el dump de verificación (--dump-costa):
// lo que ves in-game es lo que verifica el BMP.
class Palette {
public:
    explicit Palette(const TerrainData& data);

    // Color final del suelo, moteado y bandeado incluidos. wx/wz es la
    // posición real del vértice (moteado estable); cx/cz la warpeada con la
    // que se muestreó el material (los campos siguen el borde serpenteante).
    glm::vec3 ground(Material m, float forest_density, float climate, float y,
                     float wx, float wz, float cx, float cz) const;

    // Variación por faceta del estilo low-poly: modula el color base de
    // ground() con lo que distingue a dos caras vecinas — pendiente y
    // orientación al sol de la propia cara (solana seca y cálida, umbría
    // fresca), lluvia local y un pequeño salto de valor por cara (`id`).
    // n = normal de la cara; cx/cz = centroide warpeado.
    // y = cota del centroide (nieve retenida en caras altas tendidas).
    glm::vec3 face(glm::vec3 base, Material m, glm::vec3 n, float y, float cx, float cz,
                   std::uint32_t id) const;

private:
    const TerrainData* data_;
    float sea_level_ = 0.0f;
    float snow_line_ = 0.0f; // nieve estacional de referencia
    float tree_line_ = 1.0f;
    glm::vec3 sun_{0.0f, 1.0f, 0.0f}; // hacia el sol fijo del mundo
    float snow_span_ = 1.0f; // recorrido nieve sucia → blanca pura
    float rock_low_ = 0.0f;  // arranque del gradiente de roca (sobre el mar)
    float rock_span_ = 1.0f;

    // Bilineal de un campo cuadrado res² que cubre el mapa (fallback si falta).
    float field_at(const std::vector<float>& field, int res, float wx, float wz,
                   float fallback) const;
    float lithology_at(float wx, float wz) const;    // bilineal, grid macro
    float dryness_at(float wx, float wz) const;      // déficit hídrico [0,1]
    float water_level_near(float wx, float wz) const; // NaN si no hay agua
    bool littoral_near(float wx, float wz) const;     // banda de oleaje
    float fresh_water_fraction(float wx, float wz) const; // lámina dulce cerca [0,1]
    glm::vec3 rock_base(float y, float climate, float cx, float cz) const;
    glm::vec3 rock(float y, float climate, float wx, float wz,
                   float cx, float cz) const;
    glm::vec3 scree(float y, float climate, float cx, float cz) const;
    glm::vec3 snow(float y) const;
    glm::vec3 beach_sand(float y, float climate) const;
    float snow_cover(float y, float slope, float sunny) const;
};

} // namespace terrain
