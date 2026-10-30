#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace veg {

// Dosel lejano (LOD de bosque): más allá de la distancia de dibujo de los
// árboles instanciados, el bosque no puede desaparecer y dejar la ladera en
// pradera. Cada árbol colocado deposita su copa (área de la proyección
// horizontal y color de follaje con su tinte) en una rejilla de mundo; el
// terreno lejano mezcla su color de suelo con ese dosel según la cobertura.
// Es la MISMA población que se dibuja de cerca, así que los rodales, la
// composición y el otoño se leen igual a cualquier distancia.
// Solo CPU (sin OpenGL): la textura la sube VegRenderer.
class CanopyAccumulator {
public:
    // size_m: lado del dominio centrado en el origen; resolution: texeles por lado.
    CanopyAccumulator(float size_m, int resolution);

    // Copa de radio crown_radius_m (m) centrada en (wx, wz) con su color de
    // follaje lineal. Fuera del dominio se ignora.
    void add(float wx, float wz, float crown_radius_m, const glm::vec3& color);

    // RGBA por texel (fila z, columna x): rgb = color medio ponderado por área
    // de copa, a = cobertura de copa [0, 1] (área de copas / área del texel).
    std::vector<glm::vec4> resolve() const;

    int resolution() const { return resolution_; }
    float size_m() const { return size_m_; }

private:
    float size_m_;
    int resolution_;
    std::vector<glm::vec4> sum_; // rgb·área, área
};

} // namespace veg
