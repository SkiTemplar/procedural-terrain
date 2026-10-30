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
    // Relieve a escala fija (no depende del LOD de la faceta).
    struct Relief {
        float tan_slope = 0.0f;  // tanβ a ~12 m
        float concavity = 0.0f;  // > 0 canal o vaguada, < 0 cresta (anillo de 30 m)
        float sunny = 0.0f;      // insolación de la ladera a ~12 m, [-1, 1]
    };
    // Lo que una cara aporta a su color además del material.
    struct FaceCtx {
        float slope = 0.0f;  // 1 − n.y de la cara
        float sunny = 0.0f;  // insolación relativa a un llano, [-1, 1]
        float y = 0.0f;
        float cx = 0.0f;
        float cz = 0.0f;
        std::uint32_t id = 0;
        bool mountain = false; // se midió el relieve
        Relief relief;
    };
    Relief relief_at(float wx, float wz) const;
    float sunny_of(glm::vec3 n) const; // insolación relativa a un llano, [-1, 1]
    // Cobertura k en [0, 1] → decisión casi binaria por cara (ver palette.cpp).
    float pick(float k, const FaceCtx& f, std::uint32_t salt) const;
    float snow_cover(const FaceCtx& f) const;
    glm::vec3 alpine_turf(float y, float sunny) const;
    glm::vec3 mountain_rock(glm::vec3 c, const FaceCtx& f) const;
    glm::vec3 grass_outcrop(glm::vec3 c, Material m, const FaceCtx& f) const;
    glm::vec3 grass_face(glm::vec3 c, Material m, const FaceCtx& f) const;
    glm::vec3 rock_face(glm::vec3 c, Material m, const FaceCtx& f) const;
    glm::vec3 snow_face(glm::vec3 c, const FaceCtx& f) const;
    glm::vec3 face_variation(glm::vec3 c, Material m, float cx, float cz,
                             std::uint32_t id) const;
};

} // namespace terrain
