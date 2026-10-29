#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace app {

// Render fuera de pantalla: ventana GLFW OCULTA (solo aporta el contexto GL),
// framebuffer propio MSAA y lectura de píxeles. Nunca muestra ventanas ni
// toma el foco: sirve para capturas, vídeo y medidas mientras el monitor está
// ocupado. Tiempo de paso fijo: el mismo fichero de entrada da los mismos
// frames.
enum class OffscreenMode {
    Shots,  // una PNG por toma
    Record, // vuelo por spline → frames rgb24 por tubería a ffmpeg (H.264)
    Bench,  // tiempos de GPU/CPU por toma → CSV
};

struct OffscreenArgs {
    OffscreenMode mode = OffscreenMode::Shots;
    std::string input;  // fichero de tomas (Shots/Bench) o de vuelo (Record)
    std::string output; // directorio (Shots), .mp4 (Record) o .csv (Bench)
    std::optional<std::uint32_t> seed;
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int samples = 8;          // MSAA del framebuffer offscreen
    int bench_frames = 240;   // frames medidos por toma (Bench)
    std::string ffmpeg = "ffmpeg";
};

// Parsea `--shots|--record|--bench <entrada> <salida> [--seed N]
// [--size WxH] [--fps N] [--samples N] [--frames N] [--ffmpeg ruta]`.
// nullopt si argv[1] no es un modo offscreen; lanza std::invalid_argument
// ante argumentos mal formados.
std::optional<OffscreenArgs> parse_offscreen_args(int argc, char** argv);

// Ejecuta el modo; devuelve el código de salida del proceso.
int run_offscreen(const OffscreenArgs& args);

} // namespace app
