# Procedural Terrain — instrucciones del proyecto

Juego de exploración en primera persona sobre terreno procedural (C++20, OpenGL 4.6, CMake+Ninja+vcpkg). Todo emerge de simulación física (LEM stream-power, hidrología D8) — **nunca estampado**. Doble objetivo: motor/juego (isla → planeta) y **TFG de amplificación de DEMs reales** (portfolio hacia Aechelon/Shield AI).

## LO PRIMERO: lee `docs/plan-maestro.md`

Es el artefacto canónico y vivo: estado real, fases con criterios checkeables, manual de operaciones (Apéndice A), fases detalladas (Apéndice B) y track TFG (Apéndice C). Su §0 explica cómo retomar el proyecto sin ningún contexto previo. **Ante conflicto con el README (desactualizado), gana el plan.** Al cerrar trabajo, actualiza el plan — no la memoria de un modelo.

## Comandos esenciales (PowerShell, desde la raíz)

```powershell
tools\build.ps1 -Test                                  # build Debug + ctest (recarga vcvars64 siempre)
cmake-build-debug-opengl\ProceduralTerrain.exe         # jugar (4096² tarda ~150 s en Debug)
ProceduralTerrain.exe --dump-costa <prefijo> [seed]    # dumps BMP de verificación sin ventana
ProceduralTerrain.exe <seed> <x> <z> <yaw> <pitch> [alt]  # pose de cámara para capturas
cd tfg; uv run python e0/run_e0.py                     # experimentos TFG (proyecto uv aparte)
```

## Reglas duras

- **"No fakear nada"**: los rasgos salen de procesos simulados; el ruido solo modula procesos (plan §A.5).
- Determinismo: misma seed ⇒ mismo mundo. `ctest` verde antes de todo commit.
- Todo cambio de render/terreno se verifica visualmente (dump o captura, plan §A.3).
- Matar el exe antes de recompilar. Commits `tipo: descripción` en minúsculas. Rendimiento se mide SIEMPRE en Release.
- El generador no conoce OpenGL; el render no conoce el pipeline de generación.
