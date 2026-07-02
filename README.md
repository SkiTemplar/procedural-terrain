# Procedural Terrain

Juego de exploración: caminar/volar por un archipiélago generado
proceduralmente (tectónica, erosión, ríos, biomas). Fase 2: planeta esférico.

Diseño completo: `docs/superpowers/specs/2026-07-02-procedural-terrain-design.md`

## Estado

- [x] **Hito 1 — Base**: ventana GL 4.6, cámara voladora, grid flat-shaded, ImGui
- [x] **Hito 2 — Isla**: fBM + continentalidad, mar, playas, modo caminar (F)
- [ ] Hito 3 — Tectónica (placas, cordilleras, fallas)
- [x] **Hito 4 — Erosión hidráulica + ríos/lagos** *(adelantado)*
- [ ] Hito 5 — Biomas + vegetación instanciada *(clasificación de materiales ya hecha)*
- [ ] Hito 6 — Pulido (LOD, presets, perf)
- [ ] Fase 2 — Planeta cube-sphere

La isla (5×5 km por defecto, tuneable en vivo) se genera con: continentalidad
con domain warping → colinas fBM + cordilleras ridged → erosión hidráulica de
200k gotas → priority-flood + flow accumulation D8 (ríos que llegan al mar,
lagos en depresiones) → clasificación de materiales (arena/hierba/bosque/
roca/nieve/lecho). Panel ImGui: seed, tamaño, alturas, erosión, umbral de
ríos, línea de nieve y botón Regenerar.

## Controles

| Tecla | Acción |
|-------|--------|
| `WASD` + ratón | Moverse / mirar |
| `Espacio` / `Ctrl` | Subir / bajar (volando) · saltar (caminando) |
| `Shift` | Acelerar ×5 (volando) · esprintar (caminando) |
| `Esc` | Soltar/capturar ratón (para usar el panel de debug) |
| `F` | Alternar caminar↔volar |

## Build

Requiere MSVC Build Tools 2022, vcpkg en `C:/vcpkg` y Ninja en el PATH
(o usar CLion, que lo trae). Desde una consola con `vcvars64`:

```
cmake --preset Debug-OpenGL
cmake --build cmake-build-debug-opengl
cmake-build-debug-opengl\ProceduralTerrain.exe
```

## Dependencias (vcpkg manifest)

- **GLFW3** — ventana + input + contexto
- **GLAD** — loader de OpenGL (con `GLFW_INCLUDE_NONE` global)
- **GLM** — matemáticas
- **ImGui** — panel de debug (sliders, wireframe, sol)
- **GoogleTest** — tests del pipeline de generación (desde Hito 2)

## Estructura

```
src/core/     ventana, input, reloj de frame
src/render/   shader, mesh, cámara, shaders de terreno
src/terrain/  generación (Hito 1: grid placeholder)
src/ui/       overlay ImGui
```
