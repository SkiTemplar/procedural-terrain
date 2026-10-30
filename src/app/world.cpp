#include "app/world.h"


#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace app {

void SeabedTexture::release() {
    if (id != 0) {
        glDeleteTextures(1, &id);
        id = 0;
    }
    if (clip_id != 0) {
        glDeleteTextures(1, &clip_id);
        clip_id = 0;
    }
}

void SeabedTexture::rebuild(const terrain::TerrainData& data) {
    release();
    constexpr int kRes = 2048;
    const auto& hf = data.height;
    const int res = hf.resolution();
    const float sea = data.config.sea_level;
    std::vector<float> texels(static_cast<size_t>(kRes) * kRes * 2);
    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            const int sx = x * (res - 1) / (kRes - 1);
            const int sz = z * (res - 1) / (kRes - 1);
            const size_t si = static_cast<size_t>(sz) * res + sx;
            const size_t ti = (static_cast<size_t>(z) * kRes + x) * 2;
            // Calado CON SIGNO (negativo en tierra): el filtrado bilineal
            // interpola el cero entre texeles (orilla lisa, sin escalera de
            // texel) y el reflejo de la isla lee de aquí la cota del terreno.
            texels[ti] = sea - hf.data()[si];
            texels[ti + 1] = data.wave_energy.empty() ? 0.0f : data.wave_energy[si];
        }
    }
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, kRes, kRes, 0, GL_RG, GL_FLOAT,
                 texels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const terrain::WaterSurface surface = terrain::compute_water_surface(data);
    std::vector<std::uint8_t> clip(surface.quad_covered.size());
    for (size_t i = 0; i < clip.size(); ++i) {
        clip[i] = surface.quad_covered[i] != 0 ? 255 : 0;
    }
    glGenTextures(1, &clip_id);
    glBindTexture(GL_TEXTURE_2D, clip_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, res - 1, res - 1, 0, GL_RED,
                 GL_UNSIGNED_BYTE, clip.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    constexpr float kNoClip[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, kNoClip);
    glBindTexture(GL_TEXTURE_2D, 0);
}

World::World(terrain::TerrainData&& ready)
    : data(std::move(ready)),
      sampler(data),
      ground(data, sampler),
      water(terrain::build_water_mesh(data)),
      sea(terrain::build_sea_plane(data.config.size_m, data.config.sea_level)),
      vegetation(data, sampler),
      grass(data, sampler) {
    seabed.rebuild(data);
}

float World::pose_height(const CameraPose& pose) const {
    const float ground_h =
        std::max(data.height.sample(pose.x, pose.z), data.config.sea_level);
    return ground_h + pose.alt;
}

} // namespace app
