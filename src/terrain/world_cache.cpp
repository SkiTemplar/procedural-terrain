#include "terrain/world_cache.h"

#include "terrain/preset.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <type_traits>
#include <vector>

namespace terrain {

namespace {

constexpr std::uint32_t kMagic = 0x43575450; // "PTWC"
constexpr std::uint32_t kVersion = 1;

// Escritor/lector binario mínimo: escalares triviales y vectores de ellos.
// El lector arrastra un flag de error en vez de lanzar.
class Writer {
public:
    explicit Writer(std::ofstream& out) : out_(out) {}
    template <typename T>
    void pod(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        out_.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }
    template <typename T>
    void vec(const std::vector<T>& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        pod(static_cast<std::uint64_t>(v.size()));
        out_.write(reinterpret_cast<const char*>(v.data()),
                   static_cast<std::streamsize>(v.size() * sizeof(T)));
    }
    void str(const std::string& s) {
        pod(static_cast<std::uint64_t>(s.size()));
        out_.write(s.data(), static_cast<std::streamsize>(s.size()));
    }

private:
    std::ofstream& out_;
};

class Reader {
public:
    explicit Reader(std::ifstream& in) : in_(in) {}
    bool ok() const { return ok_ && static_cast<bool>(in_); }
    template <typename T>
    void pod(T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (ok()) {
            in_.read(reinterpret_cast<char*>(&v), sizeof(T));
        }
    }
    template <typename T>
    void vec(std::vector<T>& v) {
        std::uint64_t n = 0;
        pod(n);
        if (!ok() || n > kMaxElements) {
            ok_ = false;
            return;
        }
        v.resize(static_cast<size_t>(n));
        in_.read(reinterpret_cast<char*>(v.data()),
                 static_cast<std::streamsize>(v.size() * sizeof(T)));
    }
    void str(std::string& s) {
        std::uint64_t n = 0;
        pod(n);
        if (!ok() || n > kMaxElements) {
            ok_ = false;
            return;
        }
        s.resize(static_cast<size_t>(n));
        in_.read(s.data(), static_cast<std::streamsize>(s.size()));
    }

private:
    static constexpr std::uint64_t kMaxElements = 1ull << 32;
    std::ifstream& in_;
    bool ok_ = true;
};

// Un único recorrido de campos para escribir y leer: imposible que el orden
// de ida y el de vuelta diverjan.
template <typename IO, typename Data>
void visit_fields(IO& io, Data& d) {
    io.vec(d.height.data());
    io.vec(d.hydrology.water_level);
    io.vec(d.hydrology.flow);
    io.vec(d.hydrology.lake);
    io.vec(d.hydrology.whitewater);
    io.pod(d.hydrology.deposits);
    io.vec(d.material);
    io.vec(d.forest_density);
    io.vec(d.climate);
    io.vec(d.wave_energy);
    io.vec(d.lithology);
    io.pod(d.litho_res);
    io.vec(d.coast_cliff);
    io.pod(d.max_height_m);
    io.pod(d.snow_line_m);
    io.pod(d.treeline_m);
    io.vec(d.shadow);
    io.vec(d.sky_ao);
    io.pod(d.lem_stats.eroded_m3);
    io.pod(d.lem_stats.deposited_m3);
    io.pod(d.lem_stats.aggrading_cells);
    io.vec(d.lem_stats.alluvium_m);
    io.vec(d.lem_stats.rain);
    io.vec(d.lem_stats.denudation);
    io.pod(d.rockfall_stats);
    io.pod(d.light_res);
    io.pod(d.wind);
    io.vec(d.precipitation);
    io.pod(d.precip_res);
    io.vec(d.dryness);
}

std::string config_key(const TerrainConfig& cfg) {
    return preset_to_json(cfg).dump();
}

} // namespace

bool save_world_cache(const std::string& path, const TerrainData& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    Writer w(out);
    w.pod(kMagic);
    w.pod(kVersion);
    w.str(config_key(data.config));
    w.pod(data.height.resolution());
    w.pod(data.height.size());
    visit_fields(w, data);
    return static_cast<bool>(out);
}

std::optional<TerrainData> load_world_cache(const std::string& path,
                                            const TerrainConfig& expected) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    Reader r(in);
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::string key;
    int resolution = 0;
    float size_m = 0.0f;
    r.pod(magic);
    r.pod(version);
    r.str(key);
    r.pod(resolution);
    r.pod(size_m);
    if (!r.ok() || magic != kMagic || version != kVersion || key != config_key(expected)
        || resolution < 2) {
        return std::nullopt;
    }
    TerrainData data;
    data.height = Heightfield(resolution, size_m);
    visit_fields(r, data);
    const size_t cells = static_cast<size_t>(resolution) * static_cast<size_t>(resolution);
    if (!r.ok() || data.height.data().size() != cells || data.material.size() != cells) {
        return std::nullopt;
    }
    data.config = expected;
    return data;
}

} // namespace terrain
