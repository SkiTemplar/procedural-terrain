cmake_minimum_required(VERSION 3.21)
project(ProceduralTerrain CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

# Pipeline de generación: sin dependencias de OpenGL, testeable en aislamiento.
add_library(terrain_gen STATIC
        src/terrain/noise.cpp
        src/terrain/heightfield.cpp
        src/terrain/erosion.cpp
        src/terrain/hydrology.cpp
        src/terrain/riverscape.cpp
        src/terrain/coast.cpp
        src/terrain/generator.cpp
        src/terrain/detail.cpp
        src/terrain/lem.cpp
        src/terrain/preset.cpp
)
target_include_directories(terrain_gen PUBLIC src)
target_link_libraries(terrain_gen PUBLIC glm::glm nlohmann_json::nlohmann_json)

add_executable(ProceduralTerrain
        src/main.cpp
        src/core/window.cpp
        src/core/input.cpp
        src/render/shader.cpp
        src/render/camera.cpp
        src/render/mesh.cpp
        src/render/instanced_mesh.cpp
        src/terrain/mesher.cpp
        src/terrain/palette.cpp
        src/terrain/chunked_terrain.cpp
        src/veg/models.cpp
        src/veg/veg_renderer.cpp
        src/veg/grass_field.cpp
        src/player/controller.cpp
        src/ui/debug_ui.cpp
        src/ui/field_maps.cpp
)

target_include_directories(ProceduralTerrain PRIVATE src)

# GLFW no debe incluir los headers de GL: los aporta glad.
target_compile_definitions(ProceduralTerrain PRIVATE GLFW_INCLUDE_NONE)

target_link_libraries(ProceduralTerrain PRIVATE
        terrain_gen
        glfw
        glad::glad
        imgui::imgui
        OpenGL::GL
)

if (MSVC)
    target_compile_options(ProceduralTerrain PRIVATE /W4)
    target_compile_options(terrain_gen PRIVATE /W4)
else ()
    target_compile_options(ProceduralTerrain PRIVATE -Wall -Wextra)
    target_compile_options(terrain_gen PRIVATE -Wall -Wextra)
endif ()

# Tests del pipeline de generación
enable_testing()
find_package(GTest CONFIG REQUIRED)

add_executable(terrain_tests tests/terrain_tests.cpp)
target_link_libraries(terrain_tests PRIVATE terrain_gen GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(terrain_tests)
