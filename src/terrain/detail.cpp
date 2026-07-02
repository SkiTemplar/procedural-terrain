#include "terrain/detail.h"

#include <algorithm>
#include <cmath>

namespace terrain {

DetailSampler::DetailSampler(const TerrainData& data)
    : data_(&data), noise_(data.config.seed ^ 0x51ED270Bu) {}

float DetailSampler::material_roughness(Material m) {
    // Las praderas y el suelo de bosque son LISOS en la naturaleza; el grano
    // fuerte queda para el roquedo.
    switch (m) {
        case Material::Seabed: return 0.15f;
        case Material::Sand: return 0.10f;
        case Material::Grass: return 0.40f;
        case Material::Forest: return 0.60f;
        case Material::Rock: return 1.5f;
        case Material::Snow: return 0.35f;
        case Material::RiverBed: return 0.2f;
        case Material::LakeBed: return 0.15f;
    }
    return 1.0f;
}

Material DetailSampler::material_near(float wx, float wz) const {
    const auto& hf = data_->height;
    const int res = hf.resolution();
    const float half = hf.size() * 0.5f;
    const float inv_cell = 1.0f / hf.cell_size();

    const int x = std::clamp(static_cast<int>(std::round((wx + half) * inv_cell)), 0, res - 1);
    const int z = std::clamp(static_cast<int>(std::round((wz + half) * inv_cell)), 0, res - 1);
    return data_->material[static_cast<size_t>(z) * static_cast<size_t>(res)
                           + static_cast<size_t>(x)];
}

float DetailSampler::height(float wx, float wz) const {
    const float base = data_->height.sample(wx, wz);
    const float amplitude = data_->config.detail_amplitude_m;
    if (amplitude <= 0.0f) {
        return base;
    }

    const float rough = material_roughness(material_near(wx, wz));

    // Tres escalas: ondulación de ~30 m, rugosidad de ~7 m y grano de ~2 m
    // (el grano solo se aprecia en el LOD ultra, celdas de 30 cm).
    const glm::vec3 p{wx, 0.0f, wz};
    const float coarse = noise_.fbm(p * (1.0f / 30.0f), 3);
    const float fine = noise_.fbm(p * (1.0f / 7.0f) + glm::vec3{13.7f, 0.0f, -41.2f}, 2);
    const float grain = noise_.perlin(p * (1.0f / 2.2f) + glm::vec3{-77.3f, 0.0f, 29.1f});

    return base + amplitude * rough * (0.75f * coarse + 0.35f * fine + 0.08f * grain);
}

} // namespace terrain
