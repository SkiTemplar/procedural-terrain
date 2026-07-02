#include "terrain/tectonics.h"

#include <cmath>
#include <limits>
#include <random>

namespace terrain {

namespace {

// Anchos de banda de cada tipo de borde, en metros.
constexpr float kMountainBandM = 420.0f;
constexpr float kRiftBandM = 220.0f;
constexpr float kFaultBandM = 70.0f;

} // namespace

Tectonics::Tectonics(std::uint32_t seed, float domain_size_m, int plate_count) {
    std::mt19937 rng(seed ^ 0xC0FFEEu);
    // Los sitios cubren un dominio ampliado para que los bordes de placa
    // crucen la isla en ángulos variados, no solo cerca del centro.
    std::uniform_real_distribution<float> pos(-0.7f * domain_size_m,
                                              0.7f * domain_size_m);
    std::uniform_real_distribution<float> angle(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> bias(-1.0f, 1.0f);

    plates_.reserve(static_cast<size_t>(plate_count));
    for (int i = 0; i < plate_count; ++i) {
        const float a = angle(rng);
        plates_.push_back({{pos(rng), pos(rng)},
                           {std::cos(a), std::sin(a)},
                           bias(rng)});
    }
}

Tectonics::Sample Tectonics::sample(glm::vec2 pos_m) const {
    // Dos placas más cercanas: a (propia) y b (vecina del borde más próximo).
    size_t ia = 0;
    size_t ib = 0;
    float da = std::numeric_limits<float>::max();
    float db = std::numeric_limits<float>::max();
    for (size_t i = 0; i < plates_.size(); ++i) {
        const float d = glm::length(pos_m - plates_[i].site);
        if (d < da) {
            db = da;
            ib = ia;
            da = d;
            ia = i;
        } else if (d < db) {
            db = d;
            ib = i;
        }
    }

    const Plate& a = plates_[ia];
    const Plate& b = plates_[ib];

    // Distancia (aprox.) al borde entre a y b: mitad de la diferencia de
    // distancias a los dos sitios (exacta para la mediatriz).
    const float edge_dist = (db - da) * 0.5f;
    const glm::vec2 border_dir = glm::normalize(b.site - a.site);

    // Movimiento relativo en el borde.
    const glm::vec2 relative = a.drift - b.drift;
    const float convergence = glm::dot(relative, border_dir) * 0.5f;   // [-1,1]
    const float shear = std::abs(relative.x * border_dir.y
                                 - relative.y * border_dir.x) * 0.5f;  // [0,1]

    const auto band = [edge_dist](float width) {
        const float t = edge_dist / width;
        return std::exp(-t * t);
    };

    Sample out;
    out.uplift = std::max(convergence, 0.0f) * band(kMountainBandM);
    out.rift = std::max(-convergence, 0.0f) * band(kRiftBandM);
    // Falla: cizalla dominante (borde transformante), zanja estrecha.
    out.fault = shear * (1.0f - std::min(std::abs(convergence) * 2.0f, 1.0f))
              * band(kFaultBandM);
    out.bias = a.bias;
    return out;
}

} // namespace terrain
