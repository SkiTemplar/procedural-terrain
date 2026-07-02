# Procedural Terrain — Diseño

**Fecha:** 2026-07-02 · **Estado:** Aprobado por Rodrigo

## Objetivo

Juego de exploración en primera persona: caminar/volar por un terreno generado
proceduralmente que parezca *natural* — valles, cordilleras, fallas, ríos que
fluyen al mar, lagos, playas, biomas. Sin combate ni mecánicas: el terreno es
el juego.

**Fase 1 (ahora):** archipiélago finito ~8×8 km.
**Fase 2 (~2 meses):** planeta cube-sphere caminable, reutilizando los campos
de generación de la fase 1.

## Decisiones cerradas

| Tema | Decisión |
|------|----------|
| Mundo fase 1 | Isla/archipiélago finito 8×8 km, heightmap 2048² (~4 m/celda) |
| Relieve | Natural, sin exageración: cumbres 800–1200 m, pendientes reales |
| Estética | Low-poly estilizado, flat shading, color por material, sin assets externos |
| Controles | WASD + ratón. `F` alterna caminar↔volar. Volando: `Espacio` sube, `Ctrl` baja, `Shift` acelera. Caminando: gravedad + pegado al suelo, `Shift` sprint |
| Velocidades | Caminar ~5 m/s, sprint ~10, volar 30–100 m/s ajustable |
| Anti-caos de parámetros | Pocos parámetros con significado físico por etapa + ImGui con sliders en vivo, minimapas de campos intermedios y presets JSON |
| Stack | C++20, OpenGL 4.6 core, GLFW, GLAD, GLM, ImGui (vcpkg), CMake + Ninja |

## Pipeline de generación (CPU, determinista por seed)

Cada etapa consume la salida de la anterior y expone 3–5 parámetros físicos:

1. **Placas tectónicas** — Voronoi ~15 placas con vectores de deriva.
   Convergente → uplift (cordilleras); divergente → rift; transformante →
   falla. Salida: mapa de uplift.
2. **Relieve base** — Simplex fBM modulado por uplift + campo de
   continentalidad (con domain warping para costas irregulares).
3. **Erosión hidráulica** — simulación de partículas (~10⁵–10⁶ gotas):
   valles en V, laderas suaves, deposición en llanuras.
4. **Hidrología** — priority-flood (relleno de depresiones) + flow
   accumulation D8: ríos que llegan al mar de verdad, depresiones → lagos.
5. **Playas y biomas** — playa = cota ≈ nivel del mar + pendiente baja +
   sedimento. Biomas por (altura, pendiente, humedad): pradera, bosque,
   roquedo, arena, nieve.

**Restricción para fase 2:** ruido, continentalidad y tectónica se escriben
muestreando posiciones 3D (fase 1: sobre el plano; fase 2: sobre la esfera).
La erosión sobre grid queda aislada como etapa opcional — en el planeta se
sustituirá por erosión por parches.

## Renderizado

- Terreno por chunks con LOD (distancia → menos triángulos), skirts contra
  grietas. Flat shading (normal por cara).
- Agua: plano del mar transparente con olas de vértice; ríos con malla propia
  siguiendo las polilíneas de flujo.
- Vegetación/rocas generadas por código (árbol = tronco + conos/icosferas
  deformadas; roca = icosfera con ruido), instanciadas
  (`glDrawElementsInstanced`), scatter Poisson con densidad por bioma.
- Sol direccional + niebla por distancia + cielo degradado.

## Arquitectura

```
src/core/     ventana, input, tiempo, GL context
src/render/   shader, mesh, camera, instancing
src/terrain/  plates, noise, erosion, hydrology, biomes, meshing+LOD
src/veg/      generadores árbol/roca, scatter
src/player/   controller andar/volar
src/ui/       ImGui: sliders, minimapas de campos, presets JSON
```

Ficheros pequeños y cohesivos (<400 líneas objetivo). El generador no conoce
OpenGL; el render no conoce el pipeline de generación.

## Errores y validación

- Fallos de init (GLFW/GLAD/shader compile) → mensaje claro y salida limpia.
- Generación validada con tests unitarios (parte determinista):
  - misma seed → mismo heightmap (hash)
  - tras priority-flood no quedan sumideros interiores; todo río alcanza mar o lago
  - erosión conserva masa aproximada (sedimento arrancado ≈ depositado + evacuado)
- El render se valida visualmente (no tiene sentido testear píxeles aquí).

## Hitos

1. **Base**: CMake arreglado (`ProceduralTerrain`), ventana + GL 4.6, cámara
   voladora, grid flat-shaded, ImGui integrado.
2. **Isla básica**: fBM + continentalidad, mar, playas por regla simple,
   andar/volar completo con colisión de suelo.
3. **Tectónica**: placas, uplift, cordilleras, rifts, fallas.
4. **Erosión + hidrología**: gotas, ríos, lagos.
5. **Biomas + vegetación**: clasificación, árboles/rocas instanciadas.
6. **Pulido**: LOD fino, niebla, presets, rendimiento (>60 fps).
7. **Fase 2**: planeta cube-sphere (spec propio cuando lleguemos).

## No-objetivos (fase 1)

- Multijugador, guardado de partida, gameplay más allá de explorar.
- Texturas realistas / PBR.
- Streaming infinito de chunks (el mundo es finito hasta la fase 2).
