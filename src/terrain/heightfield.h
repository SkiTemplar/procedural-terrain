#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace terrain {

// Grid cuadrado de alturas centrado en el origen del mundo (XZ).
// Sin dependencias de OpenGL: testeable en aislamiento.
class Heightfield {
public:
    Heightfield(int resolution, float size_m);

    int resolution() const { return resolution_; }
    float size() const { return size_m_; }
    float cell_size() const { return size_m_ / static_cast<float>(resolution_ - 1); }

    float at(int x, int z) const { return data_[index(x, z)]; }
    float& at(int x, int z) { return data_[index(x, z)]; }

    // Interpolación bilineal en coordenadas de mundo; clampa fuera del dominio.
    float sample(float wx, float wz) const;

    // Normal por diferencias centrales (para pendiente/clasificación).
    glm::vec3 normal_at(int x, int z) const;

    // Coordenada de mundo de la muestra (x, z).
    float world_x(int x) const { return -size_m_ * 0.5f + static_cast<float>(x) * cell_size(); }
    float world_z(int z) const { return -size_m_ * 0.5f + static_cast<float>(z) * cell_size(); }

    const std::vector<float>& data() const { return data_; }
    std::vector<float>& data() { return data_; }

private:
    int resolution_;
    float size_m_;
    std::vector<float> data_;

    size_t index(int x, int z) const {
        return static_cast<size_t>(z) * static_cast<size_t>(resolution_) + static_cast<size_t>(x);
    }
};

// Diagnóstico de paredes: celdas de tierra (h > sea_level) con pendiente
// central > 1.2 (~50°) y su desglose por desnivel continuo — escarpes de
// >40 m en una ventana de radio 3 celdas y de >100 m en radio 12. Un
// acantilado legítimo aparece aquí; una muralla de terraza cortada a cuchillo
// también: la cifra se compara entre versiones del generador.
struct WallStats {
    std::size_t land = 0;
    std::size_t walls = 0;     // pendiente > 1.2
    std::size_t scarp40 = 0;   // pared con >40 m de desnivel en radio 3
    std::size_t scarp100 = 0;  // pared con >100 m de desnivel en radio 12
};
WallStats count_walls(const Heightfield& hf, float sea_level);

} // namespace terrain
