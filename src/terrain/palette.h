#pragma once

#include "terrain/generator.h"

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

private:
    const TerrainData* data_;
    float sea_level_ = 0.0f;
    float snow_line_ = 0.0f; // efectiva: min(slider, 0.60·cumbre)
    float tree_line_ = 1.0f;
    float snow_span_ = 1.0f; // recorrido nieve sucia → blanca pura
    float rock_low_ = 0.0f;  // arranque del gradiente de roca (sobre el mar)
    float rock_span_ = 1.0f;

    float lithology_at(float wx, float wz) const;    // bilineal, grid macro
    float water_level_near(float wx, float wz) const; // NaN si no hay agua
    glm::vec3 rock(float y, float climate, float wx, float wz,
                   float cx, float cz) const;
    glm::vec3 snow(float y) const;
};

} // namespace terrain
