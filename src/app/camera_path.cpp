#include "app/camera_path.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace app {

namespace {

// Recorre las líneas útiles (sin vacías ni comentarios) con su número.
template <typename Fn>
void for_each_line(std::istream& in, Fn&& fn) {
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }
        std::istringstream fields(line);
        fn(fields, number);
    }
}

[[noreturn]] void bad_line(int number, const char* what) {
    throw std::invalid_argument("linea " + std::to_string(number) + ": " + what);
}

float catmull_rom(float p0, float p1, float p2, float p3, float u) {
    const float u2 = u * u;
    const float u3 = u2 * u;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * u
                   + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u2
                   + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * u3);
}

// Desenrolla el yaw: cada clave queda a menos de 180° de la anterior.
std::vector<PathKey> unwrap_yaw(const std::vector<PathKey>& keys) {
    std::vector<PathKey> out = keys;
    for (size_t i = 1; i < out.size(); ++i) {
        float d = out[i].yaw - out[i - 1].yaw;
        while (d > 180.0f) {
            d -= 360.0f;
        }
        while (d < -180.0f) {
            d += 360.0f;
        }
        out[i].yaw = out[i - 1].yaw + d;
    }
    return out;
}

PathKey lerp_keys(const PathKey& p0, const PathKey& p1, const PathKey& p2,
                  const PathKey& p3, float u, float t) {
    PathKey k;
    k.t = t;
    k.x = catmull_rom(p0.x, p1.x, p2.x, p3.x, u);
    k.y = catmull_rom(p0.y, p1.y, p2.y, p3.y, u);
    k.z = catmull_rom(p0.z, p1.z, p2.z, p3.z, u);
    k.yaw = catmull_rom(p0.yaw, p1.yaw, p2.yaw, p3.yaw, u);
    k.pitch = catmull_rom(p0.pitch, p1.pitch, p2.pitch, p3.pitch, u);
    return k;
}

// Clave fantasma por reflexión en el extremo: tangente = la del tramo vecino.
PathKey reflect(const PathKey& edge, const PathKey& inner) {
    PathKey k;
    k.x = 2.0f * edge.x - inner.x;
    k.y = 2.0f * edge.y - inner.y;
    k.z = 2.0f * edge.z - inner.z;
    k.yaw = 2.0f * edge.yaw - inner.yaw;
    k.pitch = 2.0f * edge.pitch - inner.pitch;
    return k;
}

} // namespace

std::vector<Shot> parse_shots(std::istream& in) {
    std::vector<Shot> shots;
    for_each_line(in, [&](std::istringstream& f, int number) {
        Shot s;
        if (!(f >> s.name >> s.x >> s.z >> s.yaw >> s.pitch >> s.alt)) {
            bad_line(number, "se esperaba: nombre x z yaw pitch alt [t]");
        }
        if (!(f >> s.time_s)) {
            s.time_s = 0.0f;
        }
        shots.push_back(s);
    });
    return shots;
}

std::vector<Shot> parse_flight(std::istream& in) {
    std::vector<Shot> keys;
    for_each_line(in, [&](std::istringstream& f, int number) {
        Shot s;
        if (!(f >> s.time_s >> s.x >> s.z >> s.alt >> s.yaw >> s.pitch)) {
            bad_line(number, "se esperaba: t x z alt yaw pitch");
        }
        if (!keys.empty() && s.time_s <= keys.back().time_s) {
            bad_line(number, "t debe ser estrictamente creciente");
        }
        keys.push_back(s);
    });
    if (keys.size() < 2) {
        throw std::invalid_argument("un vuelo necesita al menos 2 claves");
    }
    return keys;
}

PathKey sample_path(const std::vector<PathKey>& raw_keys, float t) {
    if (raw_keys.empty()) {
        throw std::invalid_argument("sample_path sin claves");
    }
    const std::vector<PathKey> keys = unwrap_yaw(raw_keys);
    if (keys.size() == 1 || t <= keys.front().t) {
        return keys.front();
    }
    if (t >= keys.back().t) {
        return keys.back();
    }
    const auto next = std::upper_bound(
        keys.begin(), keys.end(), t,
        [](float value, const PathKey& k) { return value < k.t; });
    const size_t i2 = static_cast<size_t>(next - keys.begin());
    const size_t i1 = i2 - 1;
    const PathKey& p1 = keys[i1];
    const PathKey& p2 = keys[i2];
    const PathKey p0 = i1 > 0 ? keys[i1 - 1] : reflect(p1, p2);
    const PathKey p3 = i2 + 1 < keys.size() ? keys[i2 + 1] : reflect(p2, p1);
    const float u = (t - p1.t) / (p2.t - p1.t);
    return lerp_keys(p0, p1, p2, p3, u, t);
}

} // namespace app
