cmake_minimum_required(VERSION 3.21)
project(ProceduralTerrain CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(ProceduralTerrain
        src/main.cpp
        src/core/window.cpp
        src/core/input.cpp
        src/render/shader.cpp
        src/render/camera.cpp
        src/render/mesh.cpp
        src/terrain/grid.cpp
        src/ui/debug_ui.cpp
)

target_include_directories(ProceduralTerrain PRIVATE src)

# GLFW no debe incluir los headers de GL: los aporta glad.
target_compile_definitions(ProceduralTerrain PRIVATE GLFW_INCLUDE_NONE)

target_link_libraries(ProceduralTerrain PRIVATE
        glfw
        glad::glad
        glm::glm
        imgui::imgui
        OpenGL::GL
)

if (MSVC)
    target_compile_options(ProceduralTerrain PRIVATE /W4)
else ()
    target_compile_options(ProceduralTerrain PRIVATE -Wall -Wextra)
endif ()
