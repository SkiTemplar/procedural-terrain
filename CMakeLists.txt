cmake_minimum_required(VERSION 3.21)
project(Procedural Terrain CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(Procedural Terrain
        src/main.cpp
)

set(OPENGL_DEPS
        glfw
        glad::glad
        glm::glm
        OpenGL::GL
)

target_link_libraries(Procedural Terrain PRIVATE ${OPENGL_DEPS})
