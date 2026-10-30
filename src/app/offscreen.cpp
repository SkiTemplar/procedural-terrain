#include "app/offscreen.h"

#include "app/camera_path.h"
#include "app/scene_renderer.h"
#include "app/world.h"
#include "core/window.h"
#include "render/camera.h"
#include "terrain/generator.h"
#include "terrain/world_cache.h"
#include "ui/debug_ui.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace app {

namespace {

// Framebuffer de render: MSAA (color RGBA8 + depth24) que se resuelve a uno
// de una muestra para leer los píxeles. Los shaders ya tonemapean (ACES), así
// que el destino LDR es el mismo que el back buffer interactivo.
class OffscreenTarget {
public:
    OffscreenTarget(int width, int height, int samples)
        : width_(width), height_(height) {
        glGenFramebuffers(1, &msaa_fbo_);
        glGenRenderbuffers(2, msaa_rb_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaa_rb_[0]);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, msaa_rb_[1]);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                                         GL_DEPTH_COMPONENT24, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, msaa_rb_[0]);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, msaa_rb_[1]);
        check("MSAA");

        glGenFramebuffers(1, &resolve_fbo_);
        glGenRenderbuffers(1, &resolve_rb_);
        glBindRenderbuffer(GL_RENDERBUFFER, resolve_rb_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, resolve_rb_);
        check("resolve");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    ~OffscreenTarget() {
        glDeleteFramebuffers(1, &msaa_fbo_);
        glDeleteFramebuffers(1, &resolve_fbo_);
        glDeleteRenderbuffers(2, msaa_rb_);
        glDeleteRenderbuffers(1, &resolve_rb_);
    }

    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;

    void bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_);
        glViewport(0, 0, width_, height_);
    }

    // Resuelve el MSAA y lee RGB de abajo arriba (convención GL).
    void read_rgb(std::vector<unsigned char>& out) const {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo_);
        glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, resolve_fbo_);
        out.resize(static_cast<size_t>(width_) * height_ * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, out.data());
        glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_);
    }

    float aspect() const { return static_cast<float>(width_) / static_cast<float>(height_); }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    int width_;
    int height_;
    GLuint msaa_fbo_ = 0;
    GLuint msaa_rb_[2] = {0, 0};
    GLuint resolve_fbo_ = 0;
    GLuint resolve_rb_ = 0;

    static void check(const char* what) {
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error(std::string("framebuffer offscreen incompleto: ") + what);
        }
    }
};

// Mundo desde la caché si casa con el config; si no, se genera (y se guarda
// cuando se pidió caché).
terrain::TerrainData obtain_world(const terrain::TerrainConfig& cfg, const std::string& cache) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto secs = [&t0]() {
        return std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    };
    if (!cache.empty()) {
        if (auto cached = terrain::load_world_cache(cache, cfg)) {
            std::cout << "mundo cargado de la cache " << cache << " en " << secs() << " s"
                      << std::endl;
            return std::move(*cached);
        }
    }
    terrain::TerrainData data = terrain::generate(cfg, [](const char* phase) {
        std::cout << "  fase: " << phase << std::endl;
    });
    std::cout << "terreno generado en " << secs() << " s (seed " << cfg.seed << ")" << std::endl;
    if (!cache.empty() && !terrain::save_world_cache(cache, data)) {
        std::cerr << "aviso: no se pudo escribir la cache " << cache << std::endl;
    }
    return data;
}

// Todo lo que vive mientras dura el modo: contexto, mundo y renderizador.
struct Session {
    core::Window window;
    std::unique_ptr<World> world;
    std::unique_ptr<SceneRenderer> renderer;
    std::unique_ptr<OffscreenTarget> target;
    ui::DebugState state;
    render::Camera camera;

    explicit Session(const OffscreenArgs& args)
        : window(64, 64, "Procedural Terrain (offscreen)", /*visible=*/false) {
        terrain::TerrainConfig cfg = state.pending_config;
        if (args.seed) {
            cfg.seed = *args.seed;
        }
        world = std::make_unique<World>(obtain_world(cfg, args.cache));
        renderer = std::make_unique<SceneRenderer>();
        target = std::make_unique<OffscreenTarget>(args.width, args.height, args.samples);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_MULTISAMPLE);
    }

    void place(const Shot& shot) {
        CameraPose pose{shot.x, shot.z, shot.yaw, shot.pitch, shot.alt};
        camera.position = {shot.x, world->pose_height(pose), shot.z};
        camera.yaw = shot.yaw;
        camera.pitch = shot.pitch;
    }

    // Frame completo: LOD sin tope de reconstrucciones (la imagen no depende
    // de cuántos frames lleve la cámara quieta).
    void render(float time_s) {
        target->bind();
        renderer->draw(world.get(), camera, target->aspect(), state, time_s, 1 << 20);
    }
};

std::vector<Shot> read_file(const std::string& path, bool flight) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("no se pudo abrir " + path);
    }
    return flight ? parse_flight(in) : parse_shots(in);
}

int run_shots(Session& s, const OffscreenArgs& args) {
    const std::vector<Shot> shots = read_file(args.input, false);
    std::filesystem::create_directories(args.output);
    std::vector<unsigned char> pixels;
    stbi_flip_vertically_on_write(1);
    for (const Shot& shot : shots) {
        s.place(shot);
        s.render(shot.time_s);
        s.target->read_rgb(pixels);
        const std::string path = args.output + "/" + shot.name + ".png";
        if (!stbi_write_png(path.c_str(), s.target->width(), s.target->height(), 3,
                            pixels.data(), s.target->width() * 3)) {
            std::cerr << "ERROR al escribir " << path << std::endl;
            return 1;
        }
        std::cout << "toma " << shot.name << " -> " << path << std::endl;
    }
    return 0;
}

std::vector<PathKey> resolve_keys(const World& world, const std::vector<Shot>& flight) {
    std::vector<PathKey> keys;
    keys.reserve(flight.size());
    for (const Shot& k : flight) {
        const CameraPose pose{k.x, k.z, k.yaw, k.pitch, k.alt};
        keys.push_back({k.time_s, k.x, world.pose_height(pose), k.z, k.yaw, k.pitch});
    }
    return keys;
}

// La cámara nunca atraviesa el suelo aunque la spline corte una loma.
void apply_key(Session& s, const PathKey& k) {
    constexpr float kMinClearance = 4.0f; // m
    const float ground = std::max(s.world->data.height.sample(k.x, k.z),
                                  s.world->data.config.sea_level);
    s.camera.position = {k.x, std::max(k.y, ground + kMinClearance), k.z};
    s.camera.yaw = k.yaw;
    s.camera.pitch = k.pitch;
}

int run_record(Session& s, const OffscreenArgs& args) {
    const std::vector<PathKey> keys = resolve_keys(*s.world, read_file(args.input, true));
    const float t0 = keys.front().t;
    const int frames = static_cast<int>((keys.back().t - t0) * static_cast<float>(args.fps)) + 1;
    const std::string cmd = "\"" + args.ffmpeg + "\" -y -loglevel error -f rawvideo"
        " -pix_fmt rgb24 -s " + std::to_string(args.width) + "x" + std::to_string(args.height)
        + " -r " + std::to_string(args.fps) + " -i - -vf vflip -c:v libx264 -threads 4 -preset slow"
        " -crf 16 -pix_fmt yuv420p -movflags +faststart \"" + args.output + "\"";
    // cmd.exe /c quita la primera y la última comilla si hay más de dos: se
    // envuelve la orden entera en otro par para que las rutas citadas lleguen
    // intactas.
    FILE* pipe = _popen(("\"" + cmd + "\"").c_str(), "wb");
    if (!pipe) {
        std::cerr << "no se pudo lanzar ffmpeg: " << cmd << std::endl;
        return 1;
    }
    std::vector<unsigned char> pixels;
    for (int f = 0; f < frames; ++f) {
        const float t = t0 + static_cast<float>(f) / static_cast<float>(args.fps);
        apply_key(s, sample_path(keys, t));
        s.render(t);
        s.target->read_rgb(pixels);
        if (std::fwrite(pixels.data(), 1, pixels.size(), pipe) != pixels.size()) {
            std::cerr << "ffmpeg cortó la tubería en el frame " << f << std::endl;
            _pclose(pipe);
            return 1;
        }
        if (f % args.fps == 0) {
            std::cout << "frame " << f << "/" << frames << std::endl;
        }
    }
    const int rc = _pclose(pipe);
    std::cout << "vídeo " << args.output << " (" << frames << " frames, ffmpeg rc " << rc
              << ")" << std::endl;
    return rc == 0 ? 0 : 1;
}

float percentile(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    const size_t i = std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size())));
    return static_cast<float>(v[i]);
}

// Tiempo de GPU por frame con GL_TIME_ELAPSED (sin vsync ni presentación) y
// tiempo de pared CPU+GPU con glFinish. Primero se calienta la toma (LOD,
// césped y cachés del driver) y luego se mide.
int run_bench(Session& s, const OffscreenArgs& args) {
    const std::vector<Shot> shots = read_file(args.input, false);
    std::ofstream csv(args.output);
    if (!csv) {
        std::cerr << "no se pudo escribir " << args.output << std::endl;
        return 1;
    }
    csv << "toma,ancho,alto,frames,gpu_ms_mediana,gpu_ms_p95,frame_ms_mediana,frame_ms_p95,"
           "chunks_visibles\n";
    GLuint query = 0;
    glGenQueries(1, &query);
    for (const Shot& shot : shots) {
        s.place(shot);
        for (int w = 0; w < 30; ++w) {
            s.render(shot.time_s);
        }
        glFinish();
        std::vector<double> gpu_ms;
        std::vector<double> wall_ms;
        for (int f = 0; f < args.bench_frames; ++f) {
            const auto c0 = std::chrono::steady_clock::now();
            glBeginQuery(GL_TIME_ELAPSED, query);
            s.target->bind();
            s.renderer->draw(s.world.get(), s.camera, s.target->aspect(), s.state,
                             shot.time_s, 2);
            glEndQuery(GL_TIME_ELAPSED);
            GLuint64 ns = 0;
            glGetQueryObjectui64v(query, GL_QUERY_RESULT, &ns);
            glFinish();
            gpu_ms.push_back(static_cast<double>(ns) * 1e-6);
            wall_ms.push_back(std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - c0).count());
        }
        csv << shot.name << ',' << args.width << ',' << args.height << ',' << args.bench_frames
            << ',' << percentile(gpu_ms, 0.5) << ',' << percentile(gpu_ms, 0.95) << ','
            << percentile(wall_ms, 0.5) << ',' << percentile(wall_ms, 0.95) << ','
            << s.state.visible_chunks << '\n';
        std::cout << "bench " << shot.name << ": gpu mediana " << percentile(gpu_ms, 0.5)
                  << " ms, frame mediana " << percentile(wall_ms, 0.5) << " ms" << std::endl;
    }
    glDeleteQueries(1, &query);
    return 0;
}

int parse_int(const std::string& v, const char* what) {
    try {
        return std::stoi(v);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("valor no numerico para ") + what + ": " + v);
    }
}

void parse_option(OffscreenArgs& a, const std::string& key, const std::string& v) {
    if (key == "--seed") {
        a.seed = static_cast<std::uint32_t>(std::stoul(v));
    } else if (key == "--size") {
        const auto x = v.find('x');
        if (x == std::string::npos) {
            throw std::invalid_argument("--size espera WxH: " + v);
        }
        a.width = parse_int(v.substr(0, x), "--size");
        a.height = parse_int(v.substr(x + 1), "--size");
    } else if (key == "--fps") {
        a.fps = parse_int(v, "--fps");
    } else if (key == "--samples") {
        a.samples = parse_int(v, "--samples");
    } else if (key == "--frames") {
        a.bench_frames = parse_int(v, "--frames");
    } else if (key == "--ffmpeg") {
        a.ffmpeg = v;
    } else if (key == "--cache") {
        a.cache = v;
    } else {
        throw std::invalid_argument("opcion desconocida: " + key);
    }
}

} // namespace

std::optional<OffscreenArgs> parse_offscreen_args(int argc, char** argv) {
    if (argc < 2) {
        return std::nullopt;
    }
    const std::string mode = argv[1];
    OffscreenArgs a;
    if (mode == "--shots") {
        a.mode = OffscreenMode::Shots;
    } else if (mode == "--record") {
        a.mode = OffscreenMode::Record;
    } else if (mode == "--bench") {
        a.mode = OffscreenMode::Bench;
    } else {
        return std::nullopt;
    }
    if (argc < 4) {
        throw std::invalid_argument(mode + " <entrada> <salida> [opciones]");
    }
    a.input = argv[2];
    a.output = argv[3];
    for (int i = 4; i < argc; i += 2) {
        if (i + 1 >= argc) {
            throw std::invalid_argument(std::string("falta el valor de ") + argv[i]);
        }
        parse_option(a, argv[i], argv[i + 1]);
    }
    if (a.width < 16 || a.height < 16 || a.fps < 1 || a.samples < 1 || a.bench_frames < 1) {
        throw std::invalid_argument("tamano, fps, muestras o frames fuera de rango");
    }
    return a;
}

int run_offscreen(const OffscreenArgs& args) {
    Session session(args);
    switch (args.mode) {
    case OffscreenMode::Shots:
        return run_shots(session, args);
    case OffscreenMode::Record:
        return run_record(session, args);
    case OffscreenMode::Bench:
        return run_bench(session, args);
    }
    return 1;
}

} // namespace app
