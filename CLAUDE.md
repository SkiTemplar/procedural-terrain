# Procedural Terrain — instrucciones del proyecto

Juego de exploración en primera persona sobre terreno procedural (C++20, OpenGL 4.6, CMake+Ninja+vcpkg). Todo emerge de simulación física (LEM stream-power, hidrología D8) — **nunca estampado**. **Proyectín personal (refocus 2026-07-08):** el objetivo es un mundo **lo más natural y realista posible**, cerrado y shippeable en la **isla v1.0**. Sin framing de portfolio ni TFG (el antiguo track de amplificación de DEMs está archivado en `tfg/`, ver plan §Apéndice C).

## LO PRIMERO: lee `docs/plan-maestro.md`

Es el artefacto canónico y vivo: estado real, fases con criterios checkeables, manual de operaciones (Apéndice A) y fases detalladas (Apéndice B). Su §0 explica cómo retomar el proyecto sin ningún contexto previo. **Ante conflicto con el README (desactualizado), gana el plan.** Al cerrar trabajo, actualiza el plan — no la memoria de un modelo.

## Protocolo Socrático (rige aquí, ver `~/.claude/CLAUDE.md`)

La IA escribe todo el código, pero las decisiones no triviales de arquitectura, diseño o resolución de problemas las toma **Rodrigo**: presentar opciones + trade-offs y exigir elección razonada (un "ok" no vale). Lo trivial no se pregunta.

## Comandos esenciales (PowerShell, desde la raíz)

```powershell
tools\build.ps1 -Test                                  # build Debug + ctest (recarga vcvars64 siempre)
cmake-build-debug-opengl\ProceduralTerrain.exe         # jugar (4096² tarda ~150 s en Debug)
ProceduralTerrain.exe --dump-costa <prefijo> [seed]    # dumps BMP de verificación sin ventana
ProceduralTerrain.exe <seed> <x> <z> <yaw> <pitch> [alt]  # pose de cámara para capturas
```

## Reglas duras

- **"No fakear nada"**: los rasgos salen de procesos simulados; el ruido solo modula procesos (plan §A.5).
- Determinismo: misma seed ⇒ mismo mundo. `ctest` verde antes de todo commit.
- Todo cambio de render/terreno se verifica visualmente (dump o captura, plan §A.3).
- Matar el exe antes de recompilar. Commits `tipo: descripción` en minúsculas. Rendimiento se mide SIEMPRE en Release.
- El generador no conoce OpenGL; el render no conoce el pipeline de generación.
