#include "render/terrain_shader_programs.h"
#include "render/terrain_texture_data.h"
#include "terrain/generator.h"

#include <gtest/gtest.h>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>

namespace {

TEST(TerrainTextures, ManifestTieneDiezMaterialesConEscalaFisica) {
    const auto directory = render::find_terrain_texture_directory(
        std::filesystem::current_path());
    ASSERT_TRUE(directory.has_value());
    std::string error;
    const auto set = render::load_terrain_texture_set(*directory, &error);
    ASSERT_TRUE(set.has_value()) << error;
    ASSERT_EQ(set->layers.size(), terrain::kMaterialCount);
    for (const auto& layer : set->layers) {
        EXPECT_GT(layer.size_m, 0.0f) << layer.name;
        EXPECT_TRUE(std::filesystem::is_regular_file(layer.albedo_path));
        EXPECT_TRUE(std::filesystem::is_regular_file(layer.normal_path));
    }
}

TEST(TerrainTextures, MaterialMapeaALaCapaEnOrdenDelEnum) {
    for (std::size_t i = 0; i < terrain::kMaterialCount; ++i) {
        const auto material = static_cast<terrain::Material>(i);
        EXPECT_EQ(render::material_layer(material), i);
    }
}

TEST(TerrainTextures, MediaAlbedoRgbDeImagenSintetica) {
    const std::array<std::uint8_t, 8> rgba{
        0, 64, 255, 17,
        255, 128, 0, 99,
    };
    const auto mean = render::mean_albedo(rgba.data(), 2, 4);
    EXPECT_NEAR(mean.r, 0.5f, 1.0e-6f);
    EXPECT_NEAR(mean.g, 96.0f / 255.0f, 1.0e-6f);
    EXPECT_NEAR(mean.b, 0.5f, 1.0e-6f);
}

TEST(TerrainTextures, DirectorioInexistenteActivaFallback) {
    std::string error;
    const auto set = render::load_terrain_texture_set(
        std::filesystem::current_path() / "no-existe-texturas-terreno", &error);
    EXPECT_FALSE(set.has_value());
    EXPECT_FALSE(error.empty());
}

class GlslangSession {
public:
    GlslangSession() { EXPECT_TRUE(glslang::InitializeProcess()); }
    ~GlslangSession() { glslang::FinalizeProcess(); }
};

std::string compile_and_link(const render::TerrainShaderProgram& source) {
    glslang::TShader vertex{EShLangVertex};
    glslang::TShader fragment{EShLangFragment};
    const char* vertex_text = source.vertex;
    const char* fragment_text = source.fragment;
    vertex.setStrings(&vertex_text, 1);
    fragment.setStrings(&fragment_text, 1);
    vertex.setAutoMapLocations(true);
    vertex.setAutoMapBindings(true);
    fragment.setAutoMapLocations(true);
    fragment.setAutoMapBindings(true);
    vertex.setEnvInput(glslang::EShSourceGlsl, EShLangVertex,
                       glslang::EShClientOpenGL, 460);
    fragment.setEnvInput(glslang::EShSourceGlsl, EShLangFragment,
                         glslang::EShClientOpenGL, 460);
    vertex.setEnvClient(glslang::EShClientOpenGL,
                        glslang::EShTargetOpenGL_450);
    fragment.setEnvClient(glslang::EShClientOpenGL,
                          glslang::EShTargetOpenGL_450);

    const TBuiltInResource* resources = GetDefaultResources();
    const auto messages = static_cast<EShMessages>(EShMsgDefault);
    std::ostringstream log;
    bool ok = vertex.parse(resources, 460, ECoreProfile, false, false, messages);
    if (!ok) {
        log << "vertex:\n" << vertex.getInfoLog() << vertex.getInfoDebugLog();
    }
    ok = fragment.parse(resources, 460, ECoreProfile, false, false, messages) && ok;
    if (!ok && fragment.getInfoLog()[0] != '\0') {
        log << "fragment:\n" << fragment.getInfoLog() << fragment.getInfoDebugLog();
    }
    if (!ok) {
        return log.str();
    }

    glslang::TProgram program;
    program.addShader(&vertex);
    program.addShader(&fragment);
    if (!program.link(messages)) {
        log << "link:\n" << program.getInfoLog() << program.getInfoDebugLog();
    }
    return log.str();
}

TEST(Shaders, CompilanConGlslang) {
    GlslangSession glslang;
    for (const auto& shader : render::kTerrainShaderPrograms) {
        const std::string log = compile_and_link(shader);
        EXPECT_TRUE(log.empty()) << shader.name << ":\n" << log;
    }
}

} // namespace
