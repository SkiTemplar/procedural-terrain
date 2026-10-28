#include "render/terrain_shader_constants.h"
#include "render/terrain_shader_programs.h"
#include "render/terrain_texture_data.h"
#include "terrain/generator.h"

#include <gtest/gtest.h>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef PT_SOURCE_DIR
#error "terrain_tests necesita PT_SOURCE_DIR (CMakeLists.txt)"
#endif

namespace {

namespace fs = std::filesystem;

// Directorio temporal propio de cada test, borrado al salir.
class TempDir {
public:
    explicit TempDir(const std::string& name)
        : path_(fs::temp_directory_path() / ("pt_texture_tests_" + name)) {
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void write_file(const fs::path& path, const std::string& content) {
    std::ofstream out{path, std::ios::binary};
    out << content;
}

// Juego mínimo válido para load_terrain_texture_set: manifest + 20 ficheros
// (no se decodifican, solo se comprueba que existen).
void write_fake_texture_set(const fs::path& directory) {
    const std::array<const char*, terrain::kMaterialCount> names{
        "seabed", "sand", "grass", "forest", "rock",
        "snow", "riverbed", "lakebed", "gravel", "marsh",
    };
    std::string manifest = "{\"materials\": {";
    for (std::size_t i = 0; i < names.size(); ++i) {
        const std::string name = names[i];
        manifest += (i ? ", \"" : "\"") + name + "\": {\"size_m\": 2.0}";
        write_file(directory / (name + "_albedo.jpg"), "a");
        write_file(directory / (name + "_normal.jpg"), "n");
    }
    manifest += "}}";
    write_file(directory / "manifest.json", manifest);
}

TEST(TerrainTextures, ManifestTieneDiezMaterialesConEscalaFisica) {
    const auto directory = render::find_terrain_texture_directory(
        fs::path{PT_SOURCE_DIR});
    if (!directory) {
        GTEST_SKIP() << "assets/textures/terrain no descargado "
                        "(tools/fetch_textures.py)";
    }
    std::string error;
    const auto set = render::load_terrain_texture_set(*directory, &error);
    ASSERT_TRUE(set.has_value()) << error;
    ASSERT_EQ(set->layers.size(), terrain::kMaterialCount);
    for (const auto& layer : set->layers) {
        EXPECT_GT(layer.size_m, 0.0f) << layer.name;
        EXPECT_TRUE(fs::is_regular_file(layer.albedo_path));
        EXPECT_TRUE(fs::is_regular_file(layer.normal_path));
    }
}

TEST(TerrainTextures, MaterialMapeaALaCapaEnOrdenDelEnum) {
    for (std::size_t i = 0; i < terrain::kMaterialCount; ++i) {
        const auto material = static_cast<terrain::Material>(i);
        EXPECT_EQ(render::material_layer(material), i);
    }
}

TEST(TerrainTextures, SrgbALinealEnPuntosConocidos) {
    EXPECT_FLOAT_EQ(render::srgb_to_linear(0.0f), 0.0f);
    EXPECT_NEAR(render::srgb_to_linear(1.0f), 1.0f, 1.0e-6f);
    EXPECT_NEAR(render::srgb_to_linear(0.5f), 0.214041f, 1.0e-5f);
}

// Imagen sintética de dos niveles (mitad negra, mitad blanca): la media lineal
// es 0.5. Promediar en sRGB y linealizar al final daría srgb_to_linear(0.5) =
// 0.214, una media 2.3 veces menor que aclararía el terreno con el ratio.
TEST(TerrainTextures, MediaAlbedoLinealizaCadaPixelAntesDePromediar) {
    const std::array<std::uint8_t, 8> rgba{
        0, 0, 0, 17,
        255, 255, 255, 99,
    };
    const auto mean = render::mean_albedo(rgba.data(), 2, 4);
    EXPECT_NEAR(mean.r, 0.5f, 1.0e-6f);
    EXPECT_NEAR(mean.g, 0.5f, 1.0e-6f);
    EXPECT_NEAR(mean.b, 0.5f, 1.0e-6f);
    EXPECT_GT(mean.r, render::srgb_to_linear(0.5f) + 0.2f);
}

TEST(TerrainTextures, MediaAlbedoDeImagenUniformeEsSuValorLineal) {
    const std::array<std::uint8_t, 9> rgb{
        64, 128, 192,
        64, 128, 192,
        64, 128, 192,
    };
    const auto mean = render::mean_albedo(rgb.data(), 3, 3);
    EXPECT_NEAR(mean.r, render::srgb_to_linear(64.0f / 255.0f), 1.0e-6f);
    EXPECT_NEAR(mean.g, render::srgb_to_linear(128.0f / 255.0f), 1.0e-6f);
    EXPECT_NEAR(mean.b, render::srgb_to_linear(192.0f / 255.0f), 1.0e-6f);
}

TEST(TerrainTextures, DirectorioInexistenteActivaFallback) {
    const TempDir temp{"fallback"};
    std::string error;
    const auto set = render::load_terrain_texture_set(
        temp.path() / "no-existe-texturas-terreno", &error);
    EXPECT_FALSE(set.has_value());
    EXPECT_FALSE(error.empty());
}

TEST(TerrainTextures, FicheroDeCapaAusenteActivaFallback) {
    const TempDir temp{"capa_ausente"};
    write_fake_texture_set(temp.path());
    fs::remove(temp.path() / "rock_normal.jpg");
    std::string error;
    EXPECT_FALSE(render::load_terrain_texture_set(temp.path(), &error));
    EXPECT_NE(error.find("rock"), std::string::npos) << error;
}

// La caché de arrays GL se invalida solo si cambia algún fichero.
TEST(TerrainTextures, HuellaEstableSinCambiosYDistintaSiCambiaUnFichero) {
    const TempDir temp{"huella"};
    write_fake_texture_set(temp.path());
    const auto set = render::load_terrain_texture_set(temp.path());
    ASSERT_TRUE(set.has_value());
    const auto first = render::stamp_texture_set(temp.path(), *set);
    const auto second = render::stamp_texture_set(temp.path(), *set);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, *second);
    EXPECT_EQ(first->sizes.size(), 1 + 2 * terrain::kMaterialCount);

    write_file(temp.path() / "grass_albedo.jpg", "otro contenido");
    const auto changed = render::stamp_texture_set(temp.path(), *set);
    ASSERT_TRUE(changed.has_value());
    EXPECT_NE(*first, *changed);
}

class GlslangSession {
public:
    GlslangSession() { EXPECT_TRUE(glslang::InitializeProcess()); }
    ~GlslangSession() { glslang::FinalizeProcess(); }
};

struct LinkedProgram {
    std::unique_ptr<glslang::TShader> vertex;
    std::unique_ptr<glslang::TShader> fragment;
    std::unique_ptr<glslang::TProgram> program;
    std::string log; // vacío si compila y enlaza
};

// setStrings guarda el puntero al array de cadenas hasta parse(): `text`
// debe apuntar a almacenamiento estable (el campo de kTerrainShaderPrograms).
std::unique_ptr<glslang::TShader> make_shader(EShLanguage stage,
                                              const char* const* text) {
    auto shader = std::make_unique<glslang::TShader>(stage);
    shader->setStrings(text, 1);
    // Sin setEnvClient: GLSL 460 core tal cual lo compila el driver. Con
    // cliente OpenGL glslang aplica las reglas de ARB_gl_spirv (uniforms del
    // bloque por defecto con location obligatoria), que no son las del juego.
    return shader;
}

LinkedProgram compile_and_link(const render::TerrainShaderProgram& source) {
    LinkedProgram out;
    out.vertex = make_shader(EShLangVertex, &source.vertex);
    out.fragment = make_shader(EShLangFragment, &source.fragment);

    const TBuiltInResource* resources = GetDefaultResources();
    const auto messages = static_cast<EShMessages>(EShMsgDefault);
    std::ostringstream log;
    if (!out.vertex->parse(resources, 460, ECoreProfile, false, false,
                           messages)) {
        log << "vertex:\n" << out.vertex->getInfoLog()
            << out.vertex->getInfoDebugLog();
    }
    if (!out.fragment->parse(resources, 460, ECoreProfile, false, false,
                             messages)) {
        log << "fragment:\n" << out.fragment->getInfoLog()
            << out.fragment->getInfoDebugLog();
    }
    if (!log.str().empty()) {
        out.log = log.str();
        return out;
    }

    out.program = std::make_unique<glslang::TProgram>();
    out.program->addShader(out.vertex.get());
    out.program->addShader(out.fragment.get());
    if (!out.program->link(messages)) {
        log << "link:\n" << out.program->getInfoLog()
            << out.program->getInfoDebugLog();
    }
    out.log = log.str();
    return out;
}

TEST(Shaders, CompilanConGlslang) {
    GlslangSession glslang;
    for (const auto& shader : render::kTerrainShaderPrograms) {
        const LinkedProgram linked = compile_and_link(shader);
        EXPECT_TRUE(linked.log.empty()) << shader.name << ":\n" << linked.log;
    }
}

// Terreno y vegetación comparten kTerrainFrag: los samplers de detalle deben
// tener binding explícito, distinto entre sí y distinto de la unidad 0 (la del
// fondo marino y la de cualquier sampler sin binding).
TEST(Shaders, SamplersDeDetalleConBindingExplicito) {
    GlslangSession glslang;
    const std::map<std::string, int> expected{
        {"u_detail_albedo", PT_DETAIL_ALBEDO_UNIT},
        {"u_detail_normal", PT_DETAIL_NORMAL_UNIT},
        {"u_material_grid", PT_MATERIAL_GRID_UNIT},
    };
    for (const auto& shader : render::kTerrainShaderPrograms) {
        if (shader.fragment != render::kTerrainFrag) {
            continue;
        }
        const LinkedProgram linked = compile_and_link(shader);
        ASSERT_TRUE(linked.log.empty()) << shader.name << ":\n" << linked.log;
        ASSERT_TRUE(linked.program->buildReflection());
        std::map<std::string, int> found;
        for (int i = 0; i < linked.program->getNumUniformVariables(); ++i) {
            const auto& uniform = linked.program->getUniform(i);
            if (expected.count(uniform.name) != 0) {
                found[uniform.name] = uniform.getBinding();
            }
        }
        EXPECT_EQ(found, expected) << shader.name;
    }
    EXPECT_NE(PT_DETAIL_ALBEDO_UNIT, 0);
    EXPECT_NE(PT_DETAIL_NORMAL_UNIT, 0);
    EXPECT_NE(PT_MATERIAL_GRID_UNIT, 0);
}

// Las lecturas de detalle van dentro de ramas no uniformes: solo textureGrad
// (derivadas explícitas) o texelFetch; nunca texture() con derivadas implícitas.
TEST(Shaders, DetalleSinDerivadasImplicitasEnRamas) {
    const std::string_view frag{render::kTerrainFrag};
    for (const char* call : {"texture(u_detail_albedo", "texture(u_detail_normal",
                             "texture(maps"}) {
        EXPECT_EQ(frag.find(call), std::string_view::npos) << call;
    }
    EXPECT_NE(frag.find("textureGrad(u_detail_albedo"), std::string_view::npos);
    EXPECT_NE(frag.find("textureGrad(u_detail_normal"), std::string_view::npos);
}

// El warp de material del shader replica terrain::value_noise: mismas
// constantes del hash entero (las del warp se comparten por macro).
TEST(Shaders, WarpDeMaterialUsaElHashDeLaMalla) {
    const std::string_view frag{render::kTerrainFrag};
    for (const char* constant : {"374761393u", "668265263u", "1274126177u",
                                 "0xFFFFFFu"}) {
        EXPECT_NE(frag.find(constant), std::string_view::npos) << constant;
    }
    EXPECT_NE(frag.find("const float f = " PT_GLSL_STR(PT_MATERIAL_WARP_FREQ)),
              std::string_view::npos);
}

} // namespace
