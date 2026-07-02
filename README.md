# Procedural Terrain

OpenGL project scaffolded by ULTRON Control Center.

**Kind:** Simple (Hello World)

## Build

1. Open the folder in your IDE (CLion / VS Code with CMake Tools).
2. Configure with the `Debug-OpenGL` preset (`CMakePresets.json`).
3. Build: `cmake --build --preset Debug-OpenGL` or your IDE shortcut.

## Dependencies (via vcpkg manifest)

- **GLFW3** — window + input + context
- **GLAD**  — OpenGL function loader
- **GLM**   — math (vectors, matrices)

vcpkg toolchain expected at `C:/vcpkg/scripts/buildsystems/vcpkg.cmake`.
