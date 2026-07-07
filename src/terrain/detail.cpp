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
        case Material::Gravel: return 0.45f; // cantos: más rugosa que la arena
        case Material::Marsh: return 0.06f;  // llano húmedo, casi plano
    }
    return 1.0f;
}

namespace {

size_t nearest_index(const Heightfield& hf, float wx, float wz) {
    const int res = hf.resolution();
    const float half = hf.size() * 0.5f;
    const float inv_cell = 1.0f / hf.cell_size();
    const int x = std::clamp(static_cast<int>(std::round((wx + half) * inv_cell)), 0, res - 1);
    const int z = std::clamp(static_cast<int>(std::round((wz + half) * inv_cell)), 0, res - 1);
    return static_cast<size_t>(z) * static_cast<size_t>(res) + static_cast<size_t>(x);
}

} // namespace

Material DetailSampler::material_near(float wx, float wz) const {
    return data_->material[nearest_index(data_->height, wx, wz)];
}

float DetailSampler::forest_density_near(float wx, float wz) const {
    return data_->forest_density[nearest_index(data_->height, wx, wz)];
}

float DetailSampler::climate_near(float wx, float wz) const {
    return data_->climate[nearest_index(data_->height, wx, wz)];
}

glm::vec3 DetailSampler::normal_near(float wx, float wz) const {
    const auto& hf = data_->height;
    const size_t i = nearest_index(hf, wx, wz);
    const int res = hf.resolution();
    return hf.normal_at(static_cast<int>(i % static_cast<size_t>(res)),
                        static_cast<int>(i / static_cast<size_t>(res)));
}

float DetailSampler::roughness_at(float wx, float wz) const {
    const auto& hf = data_->height;
    const int res = hf.resolution();
    const float half = hf.size() * 0.5f;
    const float inv_cell = 1.0f / hf.cell_size();
    const float fx = std::clamp((wx + half) * inv_cell,
                                0.0f, static_cast<float>(res - 1));
    const float fz = std::clamp((wz + half) * inv_cell,
                                0.0f, static_cast<float>(res - 1));
    const int x0 = std::min(static_cast<int>(fx), res - 2);
    const int z0 = std::min(static_cast<int>(fz), res - 2);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);
    const auto rough = [&](int x, int z) {
        return material_roughness(
            data_->material[static_cast<size_t>(z) * static_cast<size_t>(res)
                            + static_cast<size_t>(x)]);
    };
    const float top = std::lerp(rough(x0, z0), rough(x0 + 1, z0), tx);
    const float bot = std::lerp(rough(x0, z0 + 1), rough(x0 + 1, z0 + 1), tx);
    return std::lerp(top, bot, tz);
}

float DetailSampler::height(float wx, float wz) const {
    const float base = data_->height.sample(wx, wz);
    const float amplitude = data_->config.detail_amplitude_m;
    if (amplitude <= 0.0f) {
        return base;
    }

    const float rough = roughness_at(wx, wz);

    // Tres escalas: ondulación de ~30 m, rugosidad de ~7 m y grano de ~2 m
    // (el grano solo se aprecia en el LOD ultra, celdas de 30 cm).
    const glm::vec3 p{wx, 0.0f, wz};
    const float coarse = noise_.fbm(p * (1.0f / 30.0f), 3);
    const float fine = noise_.fbm(p * (1.0f / 7.0f) + glm::vec3{13.7f, 0.0f, -41.2f}, 2);
    const float grain = noise_.perlin(p * (1.0f / 2.2f) + glm::vec3{-77.3f, 0.0f, 29.1f});

    return base + amplitude * rough * (0.75f * coarse + 0.35f * fine + 0.08f * grain);
}

} // namespace terrain
