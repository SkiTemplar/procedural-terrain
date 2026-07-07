# Procedural Terrain — Plan Maestro (Hiper Plan)

> **Generado:** 2026-07-06 · modo HIGH · skill hiper-plans
> **Canónico:** `docs/plan-maestro.md` (este fichero, en el repo) · copia en `~/.ultron/plans/2026-07-06-procedural-terrain-macro-plan.md`
> **Estado:** ACTIVO
> **Propósito:** que cualquier persona o agente (Sonnet, Opus, humano) pueda continuar el proyecto **sin contexto previo de conversación**. Todo lo necesario está aquí o enlazado desde aquí.

> **Actualización 2026-07-07 — Track TFG decidido:** el trabajo del año / candidato a TFG es la **amplificación de DEMs reales con constraints hidrológicas** (pipeline estilo Aechelon: DEM real → LEM como prior físico → render en tiempo real). Informe de viabilidad, estado del arte, datasets y plan de experimentos en `docs/investigacion-amplificacion-dem.md`. Consecuencias para este plan: las Fases A–C siguen intactas (son el motor que el TFG reutiliza) y se cierran antes de abrir el track; el track TFG irá en rama propia tras `v1.0-isla`; la Fase 2 gana la opción de ser "la Tierra real caminable" (decisión en D0). **El detalle ejecutable del track TFG está en el Apéndice C.**

---

## 0. CÓMO RETOMAR ESTE PROYECTO SIN CONTEXTO (léeme primero)

Si estás leyendo esto sin acceso a las conversaciones previas (otro modelo, otra sesión, otro humano), este es el orden de arranque:

1. **Lee este plan entero.** Es el artefacto canónico: estado, fases, criterios y manual de operaciones. Ante conflicto entre este plan y el README, **gana este plan**.
2. **Documentos de soporte, en este orden:**
   - `docs/superpowers/specs/2026-07-02-procedural-terrain-design.md` — el diseño aprobado del juego/motor.
   - `docs/investigacion-agua-v2.md` — algoritmos y fuentes de los 9 pasos de agua (quedan los pasos 8 ✅hecho y 9 ❌pendiente).
   - `docs/investigacion-amplificacion-dem.md` — el informe del TFG (encuadre, estado del arte, gap vs Schott 2024, datasets, métricas, experimentos E0-E6).
   - `tfg/e0/resultados-e0.md` — números de referencia ya medidos del experimento E0.
   - `docs/investigacion-lem.md` — trasfondo del LEM si tocas `terrain/lem`.
3. **Estado a 2026-07-07:** Fase A del motor a medias (**A1 hecho, A2 oxbows pendiente** — es el siguiente paso del motor); track TFG con E0 hecho y pendiente de tutor (Apéndice C).
4. **Comandos esenciales (desde la raíz del repo, en PowerShell):**
   - Compilar + tests: `tools\build.ps1 -Test` (recarga vcvars64 siempre; ver A.1).
   - Ejecutar: `cmake-build-debug-opengl\ProceduralTerrain.exe [seed [x z yaw pitch [alt]]]` (pose de cámara para capturas; ver A.3).
   - Dump de verificación sin ventana: `ProceduralTerrain.exe --dump-costa <prefijo> [seed]` → BMPs de materiales y oleaje.
   - Captura de pantalla: `tools\capture.ps1 -Out foto.png`.
   - Experimentos TFG: `cd tfg; uv run python e0/run_e0.py` (proyecto uv autocontenido).
5. **Reglas de oro que NO se negocian:** "no fakear nada" (A.5), determinismo por seed (los tests lo protegen), tests verdes antes de commit, verificación visual de todo cambio de render/terreno (A.3), commits pequeños `tipo: descripción` en minúsculas.
6. **Objetivo estratégico del autor:** portfolio de Ingeniero de Gráficos orientado a **Aechelon Technology** (generación de imagen en tiempo real para simuladores, bases de datos geo-específicas; adquirida por Shield AI). Al priorizar trabajo, pesa más lo que demuestra ese perfil (TFG, geo-específico, whole-earth, rendimiento) que el pulido de juego.

---

## 1. PROBLEM

El proyecto tiene el diseño aprobado (`docs/superpowers/specs/2026-07-02-procedural-terrain-design.md`) y ~70 % de la Fase 1 implementada, pero el conocimiento de *qué queda, en qué orden y con qué criterios de aceptación* vive disperso entre la conversación con el modelo, el historial de git y dos informes de investigación. Si se pierde el acceso al modelo actual, se pierde el rumbo. Este documento consolida el objetivo final y las fases restantes en un artefacto ejecutable y persistente.

**Objetivo final del proyecto:** un juego de exploración en primera persona sobre un **planeta procedural caminable** (cube-sphere), con relieve, ríos, lagos, costas y biomas que **emergen de simulación física** (LEM stream-power, erosión, hidrología) — nunca estampados. Estética low-poly flat-shaded, cero assets externos, todo generado por código y determinista por seed. La Fase 1 (isla 10×10 km, actual) es el banco de pruebas de los campos de generación; la Fase 2 los lleva a la esfera. No hay combate ni mecánicas: **el terreno es el juego**.

## 2. NON-GOALS

- Multijugador, guardado de partida, gameplay más allá de explorar.
- Texturas realistas / PBR / normal maps — el flat shading por material es decisión estética cerrada.
- Assets externos (modelos, texturas, sonidos importados). Todo por código.
- Streaming infinito de chunks en Fase 1 (el mundo es finito hasta la Fase 2).
- Generación en GPU (compute). El pipeline es CPU, offline, determinista. Solo se reconsiderará en Fase 2 si los parches del planeta lo exigen (decisión en spec D0).
- Migrar de OpenGL a Vulkan/DX12.
- Editor de mundo. El panel ImGui + presets JSON es toda la herramienta.

## 3. CONSTRAINTS

| Tipo | Duro (no cambia) | Blando (negociable) |
|---|---|---|
| Arquitectura | El generador no conoce OpenGL; el render no conoce el pipeline de generación. Módulos: `core/ render/ terrain/ veg/ player/ ui/` | Ficheros <400 líneas objetivo (800 máx) |
| Determinismo | Misma seed ⇒ mismo mundo (test de hash existente debe seguir verde) | — |
| Filosofía | **No fakear nada**: los rasgos (lagos, cascadas, deltas, playas) salen de la simulación; el ruido solo modula procesos, nunca pinta resultados | Ajustes cosméticos locales (p. ej. breaching del labio de un lago) permitidos si respetan la física |
| Estética | Low-poly, flat shading, color por material, sin assets | Paleta y niebla tuneables |
| Rendimiento | >60 fps en Release con el mundo completo | Tiempo de generación 4096²: objetivo <30 s Release (hoy es asíncrono con progreso, tolerable más) |
| Stack | C++20, OpenGL 4.6 core, GLFW+GLAD+GLM+ImGui vía vcpkg manifest, CMake+Ninja, GoogleTest | Añadir deps de vcpkg si hace falta (p. ej. nlohmann-json para presets) |
| Validación | Tests deterministas del pipeline (`tests/terrain_tests.cpp`) verdes; render se valida visualmente con capturas | — |

## 4. INPUTS (estado verificado a 2026-07-06)

**Documentos (leer antes de tocar código):**
- Spec de diseño aprobado: `docs/superpowers/specs/2026-07-02-procedural-terrain-design.md`
- Investigación Agua v2 (9 pasos, con fuentes y parámetros): `docs/investigacion-agua-v2.md`
- Investigación LEM: `docs/investigacion-lem.md`
- Manual de operaciones (build/verificación/commits): **Apéndice A** de este documento.

**Código (`src/`), estado real:**

| Módulo | Ficheros | Estado |
|---|---|---|
| Núcleo | `core/window`, `core/input`, `core/frame_clock` | ✅ Hito 1 completo |
| Render | `render/shader`, `mesh`, `instanced_mesh`, `camera`, `terrain_shaders.h` | ✅ con niebla, sol, flat shading |
| Terreno | `terrain/noise`, `heightfield`, `lem`, `erosion`, `hydrology`, `riverscape`, `coast`, `generator`, `terrain_config`, `detail`, `mesher`, `chunked_terrain` | ✅ mundo 10×10 km, heightfield 4096² (~2.4 m/celda), LEM O(n) Braun-Willett + uplift anisótropo, erosión de gotas, priority-flood + D8, clima regional. **LOD por chunks ya existe**. 10 materiales (con `Gravel`/`Marsh`), `wave_energy` en `TerrainData` |
| Agua v2 | `riverscape.*` + `coast.*` | ✅ pasos 1–8 de 9 (efluentes de lagos, Leopold & Maddock, lecho monótono, cascadas y rápidos, confluencias, deltas, islotes fluviales, **playas por energía de oleaje**). ❌ paso 9 (oxbows) pendiente |
| Herramientas | `tools/build.ps1`, `tools/capture.ps1`, `tools/wave_stats.py`, `tools/material_stats.py`, modo `--dump-costa` del exe | ✅ lazo de build/verificación reproducible sin IDE (ver A.1/A.3) |
| TFG | `tfg/` (proyecto uv: e0/, papers/) | ✅ E0 hecho con números de referencia; ver Apéndice C |
| Vegetación | `veg/models` (pino, frondoso, roca, mata, arbusto, tronco caído), `veg/veg_renderer` | ✅ instanciado, culling por celdas de 512 m, colocación determinista pisando el suelo. Pendiente: auditoría de densidades/variedad por bioma |
| Player | `player/controller` | ✅ andar/volar completo (F alterna, colisión de suelo) |
| UI | `ui/debug_ui` | ✅ sliders en vivo + regenerar. ❌ sin presets JSON, ❌ sin minimapas de campos |
| Tests | `tests/terrain_tests.cpp` | ✅ existen (hash de seed, invariantes de hidrología) |

**Nota histórica:** el Hito 3 original (placas tectónicas Voronoi) fue **sustituido** por el LEM stream-power como esqueleto del relieve (commit `233cf00` "fuera placas"). No reimplementar placas: decisión cerrada.

**Estado del README:** desactualizado respecto al código (dice Hito 5 pendiente; la vegetación ya está mayormente implementada). Fiarse de esta tabla, no del README.

## 5. MVP

**MVP del plan = Fase 1 cerrada y shippeable:** isla 10×10 km con Agua v2 completa (9/9), vegetación por biomas auditada, presets JSON con 3–4 mundos de demo, minimapas de depuración y >60 fps en Release. Eso es un juego de exploración completo por sí mismo. La Fase 2 (planeta) es extensión sobre ese MVP, nunca precondición.

## 6. FAIL-FAST ORDER — mapa de fases

Ordenadas por dependencia y riesgo. **El detalle ejecutable de cada fase está en el Apéndice B.**

| # | Fase | Contenido | Riesgo | ETA (sesiones) |
|---|---|---|---|---|
| A | Agua v2 — cierre | ~~A1 playas por energía de oleaje~~ ✅ (2026-07-07) · A2 oxbows (paso 9) **← SIGUIENTE** | A2 **ALTO** (muta la traza en planta, puede romper el D8) | 1–2 restantes |
| B | Vegetación — auditoría | Densidades/variedad por bioma, uso real de los 6 modelos, transiciones | BAJO | 1 |
| C | Hito 6 — pulido | C1 presets JSON · C2 minimapas de campos · C3 rendimiento >60 fps · C4 pulido visual | MEDIO (C3) | 3–4 |
| D | Fase 2 — planeta | D0 spec propio → D1 cube-sphere+LOD → D2 campos 3D → D3 relieve por parches → D4 hidrología → D5 player esférico → D6 atmósfera | **MUY ALTO** (D1/D3/D4) | 15–25 |
| E | Release | Build Release empaquetada, README final, capturas, presets de demo | BAJO | 1 |

Reglas de tránsito entre fases:
- No se abre una fase con la anterior en rojo (tests o criterios de la §7 sin cumplir).
- **Antes de empezar D: crear tag `v1.0-isla`** — es el punto de retorno seguro.
- Dentro de D, D0 (spec) es bloqueante: no se escribe código de planeta sin spec aprobado por Rodrigo.
- E puede ejecutarse tras C (release de la isla) y repetirse tras D (release del planeta).

## 7. SUCCESS CRITERIA (checkeables)

**Fase A:**
- [ ] `ctest --test-dir cmake-build-debug-opengl --output-on-failure` verde, incluyendo test nuevo: tras estampar oxbows/barras, todo cauce D8 sigue alcanzando mar o lago (sin sumideros nuevos).
- [x] Captura de costa: se distinguen ≥3 tipos de orilla (playa disipativa ancha, grava/acantilado expuesto, marisma/roca abrigada) correlacionados con el mapa de energía de oleaje (visible en minimapa o dump). **Cumplido 2026-07-07**: dump por octantes (E 0.57 barlovento NO vs 0.15 sotavento), 4 regímenes presentes (arena 112k / grava 18k / marisma 71k celdas) y captura 3D de costa sur con disipativa+marisma.
- [ ] Captura de tramo llano: ≥1 oxbow (lago en herradura) con tapones de sedimento, y el río activo sigue fluyendo al mar.

**Fase B:**
- [ ] Los 6 modelos de `veg/models.h` aparecen en el mundo (verificable con contador por tipo en el panel ImGui).
- [ ] Densidad respeta bioma: sin árboles en arena/roca/nieve/agua; matas en pradera; troncos caídos solo en bosque.

**Fase C:**
- [ ] Botones ImGui Guardar/Cargar preset escriben/leen `presets/*.json`; cargar un preset y regenerar reproduce el mundo (mismo hash de heightfield que al guardarlo).
- [ ] ≥4 minimapas de campos intermedios visibles en ImGui (altura, flow accumulation, humedad, biomas).
- [ ] En Release, cámara volando a 100 m/s sobre la isla: fps mínimo ≥60 (medido con el frame_clock, mostrado en ImGui).
- [ ] Generación 4096² en Release ≤30 s (cronometrada en el log de progreso).

**Fase D (criterios por sub-hito en Apéndice B.4; los globales):**
- [ ] Se puede volar desde órbita hasta la superficie y **caminar** sobre el planeta sin grietas, sin jitter de precisión y sin costuras visibles en las 12 aristas del cubo.
- [ ] Misma seed ⇒ mismo planeta (test de hash sobre parches muestreados).

**Fase E:**
- [ ] `cmake --build cmake-build-release-opengl` limpio; el exe corre en una máquina sin vcpkg (deps estáticas o DLLs junto al exe).
- [ ] README actualizado con estado real, controles y 3+ capturas.

## 8. ROLLBACK

- Commits pequeños por sub-tarea (convención existente: `feat:`/`fix:` + descripción en minúsculas). Cada sub-tarea de apéndice = 1–2 commits como máximo.
- Fallo en una sub-tarea ⇒ `git revert <commit>` (preferido) o `git reset --hard HEAD~1` si aún no se ha empujado nada encima.
- **A2 (oxbows) es el rollback más probable:** si tras 2 intentos el estampado rompe el D8 o el resultado es feo, se descarta el paso entero (es el 9/9 del informe por algo) y se documenta en este fichero.
- Antes de Fase D: `git tag v1.0-isla && git push --tags`. Todo el trabajo de planeta va en rama `fase2-planeta`; `main` queda con la isla estable.
- No hay BD ni estado externo: el rollback es siempre puro git.

## 9. TOKEN BUDGET (si ejecuta un agente)

| Fase | Estimación |
|---|---|
| A | 60–100 K (A2 puede necesitar iteración visual) |
| B | 20–30 K |
| C | 60–90 K |
| D | 400–700 K (repartido en 6 sub-hitos, cada uno su sesión) |
| E | 15–25 K |

Referencia de modo: MEDIUM ~3 K · HIGH ~8 K · ULTRA ~15 K por turno de planificación. La iteración visual (captura → ajuste → captura) es lo que dispara el gasto: presupuestar 3–5 ciclos por rasgo visual nuevo.

## 10. VERIFICATION QUESTIONS (auto-chequeo CoVe)

- [x] ¿El plan asume algo que no está en INPUTS? — No: el estado de código se verificó fichero a fichero el 2026-07-06 (tabla §4). El único supuesto es que los tests actuales están verdes; el primer paso de la Fase A es comprobarlo.
- [x] ¿Cada criterio de §7 es checkeable y no interpretable? — Sí: comandos, contadores, hashes, fps medidos y capturas con rasgo concreto que buscar.
- [x] ¿El ROLLBACK cubre el peor caso? — Sí: el peor caso real es A2 (descarte documentado) y D (rama + tag aíslan `main`).
- [x] ¿FAIL-FAST ataca primero el mayor bloqueo? — Sí dentro de Fase 1 (A2 es lo más arriesgado pendiente). D0 (spec) antepone la decisión al código en Fase 2.
- [x] ¿Algún paso exige decisión humana que no esté en OPEN QUESTIONS? — D0 entero es decisión humana y está declarado bloqueante. El resto tiene defaults.
- [x] ¿Los NON-GOALS son realmente out-of-scope? — Sí: todos vienen del spec aprobado; "generación GPU" se añadió con su cláusula de reapertura explícita (D0).
- [x] ¿El TOKEN BUDGET es consistente con el modo y las ETAs? — Sí: escala con las sesiones estimadas y con el coste conocido de la iteración visual.

## 11. OPEN QUESTIONS

| ID | Pregunta | Default si no hay respuesta | Owner |
|---|---|---|---|
| HP-1 | ¿El objetivo final añade algo más al "explorar" (foto-modo, ciclo día/noche, puntos de interés)? | No. Explorar es el juego. Día/noche solo como stretch en D6 | Rodrigo |
| HP-2 | ¿Se mantiene ~sept 2026 como arranque de Fase 2? | Sí, tras cerrar C (y E si apetece release de la isla) | Rodrigo |
| HP-3 | ¿Oxbows (A2) compensa su riesgo? | Sí, se intenta; 2 intentos máximo y descarte documentado si rompe el D8 | Rodrigo |
| HP-4 | Presets JSON: ¿nlohmann-json por vcpkg o serializador a mano? | nlohmann-json (battle-tested, header-only, ya en vcpkg) | Rodrigo |

---
---

# APÉNDICE A — Manual de operaciones (sin Fable)

Todo lo operativo que no está en el README y que hace falta para trabajar en este repo desde Windows.

## A.1 Build (MSVC + Ninja)

- Requiere MSVC Build Tools 2022, vcpkg en `C:/vcpkg`, Ninja (está en `C:\vcpkg\downloads\tools`, añadirlo al PATH si no se usa CLion).
- `vcvars64` necesita `vswhere` en el PATH.
- **Usar PowerShell**, no `cmd /c` desde Git Bash (la conversión MSYS de `/c` lo rompe).
- **El entorno de vcvars64 NO persiste entre invocaciones de shell** (cada llamada de un agente es una shell nueva). Usar siempre el wrapper, que lo recarga:

```powershell
tools\build.ps1 -Test            # Debug + ctest
tools\build.ps1 -Target release  # Release (para medir rendimiento SIEMPRE en Release)
```

- Configurar la primera vez (si no existen los directorios de build): consola con vcvars64 cargado → `cmake --preset Debug-OpenGL` / `cmake --preset Release-OpenGL`.
- **Matar el exe antes de recompilar** (el linker falla si el binario está en uso): `taskkill /IM ProceduralTerrain.exe /F`.

## A.2 Tests

```powershell
cmake --build cmake-build-debug-opengl
ctest --test-dir cmake-build-debug-opengl --output-on-failure
```

Invariantes que protegen: misma seed ⇒ mismo hash de heightmap; tras priority-flood no quedan sumideros interiores; todo río alcanza mar o lago. **Cualquier cambio en `terrain/` debe dejar esto verde o actualizar el test con justificación.**

## A.3 Verificación visual (obligatoria para todo cambio de render/terreno)

El render no se testea con píxeles: se mira. Dos vías complementarias, ambas probadas:

**Vía rápida — dump sin ventana (ideal para materiales/campos):**
1. `cmake-build-debug-opengl\ProceduralTerrain.exe --dump-costa <prefijo> [seed]` → escribe `<prefijo>-materiales.bmp` y `<prefijo>-oleaje.bmp` a 4096² (~150 s en Debug, sin abrir ventana).
2. Recortar zonas a resolución nativa (System.Drawing) o reducir a ~1400² para vista general.
3. Análisis numérico si hace falta: `tools\wave_stats.py <png-oleaje>` (energía media por octante de brújula — así se cazó el bug de convención de viento) y `tools\material_stats.py <bmp-materiales>` (histograma exacto por color de paleta). Correr con el uv de `tfg/` (`cd tfg; uv run python ..\tools\wave_stats.py <ruta-absoluta>`).

**Vía completa — captura 3D in-game:**
1. Lanzar el exe con pose de cámara: `ProceduralTerrain.exe <seed> <x> <z> <yaw> <pitch> [alt]` — coordenadas de mundo en metros (mundo ≈ (grid − res/2) × 2.44 a 4096²; el centro del mapa es (0,0)). **Convención de yaw: 0 = +x (este del mapa), 90 = +z (sur, abajo en los dumps); pitch en grados, negativo mira hacia abajo; alt = metros sobre el terreno.**
2. Esperar la generación (**~150 s** en Debug a 4096²) y capturar: `tools\capture.ps1 -Out foto.png`.
3. Comparar contra el criterio de aceptación de la sub-tarea (§7 / Apéndice B). Iterar: presupuestar 3–5 ciclos por rasgo visual nuevo.
4. Al terminar: `taskkill /IM ProceduralTerrain.exe /F` antes de recompilar.

## A.4 Git

- Mensajes: `<tipo>: <descripción>` en minúsculas, tipos `feat|fix|refactor|docs|test|chore|perf`.
- Commits multilínea desde PowerShell: **no** usar here-string con `@` en el texto; escribir el mensaje a un fichero del scratchpad y `git commit -F <fichero>`.
- Rama `main` para Fase 1. Fase 2 en rama `fase2-planeta` tras taggear `v1.0-isla`.

## A.5 Principio rector (el más importante)

> **"No fakear nada."** Diseño emergente: los rasgos salen de la simulación; el ruido solo modula procesos (uplift, humedad, resistencia), nunca estampa resultados. Si una feature pide "pintar" un lago/cascada/delta directamente, la respuesta es rediseñar el proceso que lo produce. Excepción tolerada: retoques cosméticos locales derivados de un resultado físico ya calculado (p. ej. la muesca del labio de un lago cuyo saddle ya dio el priority-flood).

---

# APÉNDICE B — Fases super detalladas

## B.1 · FASE A — Cierre de Agua v2 (pasos 8–9 de `docs/investigacion-agua-v2.md`)

**Precondición:** build y `ctest` verdes tal cual está el repo. Verificar antes de tocar nada.

### A1 — Variedad de playa por energía de oleaje (paso 8) — ✅ HECHO (2026-07-07)

Implementado en `src/terrain/coast.{h,cpp}` (REI/fetch 16 rayos + rosa de vientos en convención brújula + aporte fluvial por desembocaduras + blur de contorno + banda litoral BFS). Materiales nuevos `Gravel` y `Marsh`; clasificación de 4 regímenes en `classify_materials`; sliders viento/focalización/anchura; campo `wave_energy` en `TerrainData` (para el minimapa de C2). Verificado: 22/22 tests (2 nuevos de costa), dump por octantes (E media 0.57 barlovento NO vs 0.15 sotavento), captura 3D de costa sur con disipativa+marisma. Herramienta nueva `--dump-costa` (BMP de materiales y oleaje sin ventana) para el lazo de verificación. Nota: cazado y corregido desbordamiento de `counts[8]` en main.cpp al crecer el enum (RTC stack corruption).

Detalle algorítmico completo en `docs/investigacion-agua-v2.md` §8 (REI/fetch). Resumen de integración:

1. **Mapa de energía de oleaje** (nuevo campo, probablemente en `hydrology.cpp` o fichero nuevo `coast.cpp` si supera ~200 líneas): para cada celda de costa, lanzar 16 rayos (paso 4 celdas, cap ~2.5 km) y ponderar por la rosa de vientos del clima regional ya existente. Salida: `E` por celda de costa.
2. **Clasificación de orilla** con (E, pendiente, aporte fluvial de desembocaduras cercanas): E alto + pendiente alta → grava/acantilado; E alto + aporte → playa disipativa ancha; E bajo + aporte → marisma con charcas; E bajo sin aporte → roca.
3. **Anchura de playa** = f(E, S) con blur 1D a lo largo del contorno de costa (evita cambios bruscos de material entre celdas vecinas).
4. Exponer 2–3 parámetros físicos en `terrain_config.h` + sliders en `debug_ui` (p. ej. dirección/fuerza del viento dominante, escala de anchura).
5. Materiales nuevos (grava, marisma) → paleta en `terrain_shaders.h`.

- Commits sugeridos: `feat: mapa de energia de oleaje REI/fetch` · `feat: playas por energia — grava, disipativa, marisma y roca`.
- Aceptación: criterio §7-A capturas; el test de hash cambia (regenerar hash esperado con justificación en el commit).

### A2 — Oxbows / meandros (paso 9) — riesgo ALTO, va el último porque muta la traza en planta

Detalle en `docs/investigacion-agua-v2.md` §9. Referencia C++: `aparis69/Meandering-rivers` (SIGGRAPH Asia 2023). Integración:

1. Seleccionar tramos candidatos: llanos (S < ~0.2 %), anchos, lejos de knickpoints (datos ya disponibles en `riverscape.cpp`).
2. Extraer polilínea del tramo, aplicar migración Howard-Knutson (curvatura ponderada aguas arriba, kernel exponencial) unas N iteraciones.
3. Cutoff por proximidad (< 1–2 anchuras entre nodos no adyacentes) → el bucle cortado se convierte en oxbow: tapones de sedimento en los extremos + point-bars en la orilla interna.
4. Re-estampar el cauce migrado en el heightfield y **re-validar D8**: el río debe seguir llegando al mar (test nuevo de §7-A).
5. Salvaguarda: si la validación falla, revertir el tramo a su traza original (mantener la traza previa en memoria hasta validar).

- Commits: `feat: migracion de meandros howard-knutson` · `feat: oxbows con cutoff, tapones y point-bars`.
- **Límite duro: 2 intentos.** Si no sale, descartar, documentar aquí y cerrar Agua v2 en 8/9.
- Verificación: `--dump-costa` antes/después para comparar la traza en planta (los oxbows deben verse como lagos en herradura junto al cauce activo), test nuevo de §7-A (todo cauce D8 sigue alcanzando mar o lago) y captura 3D con pose CLI sobre un tramo llano (A.3).

## B.2 · FASE B — Auditoría de vegetación (cierre real del Hito 5)

La infraestructura ya existe (`veg/models` con 6 modelos, `veg_renderer` con instancing + culling). Esto es auditoría y retoque, no construcción:

1. Añadir al panel ImGui un contador de instancias por tipo (pino, frondoso, roca, mata, arbusto, tronco).
2. Verificar en vuelo: los 6 tipos aparecen; si alguno no se usa (p. ej. `make_log`), integrarlo en la colocación con su regla de bioma (troncos solo en bosque).
3. Revisar reglas de densidad contra biomas: nada de árboles en arena/roca/nieve/lecho; gradiente en el borde del bosque (transición, no línea binaria); matas y arbustos en pradera.
4. Variación por instancia (escala/rotación/tinte) si se ve repetición evidente en las capturas.
5. Verificación visual A.3 con capturas de: borde de bosque, pradera, orilla, alta montaña.

- Commit: `feat: auditoria de vegetacion — densidades por bioma y los 6 modelos en uso`.

## B.3 · FASE C — Hito 6: pulido

### C1 — Presets JSON

1. Añadir `nlohmann-json` al manifest de vcpkg (HP-4 default).
2. Serializar/deserializar `terrain_config.h` completo (incluida la seed) ↔ `presets/<nombre>.json`.
3. UI en `debug_ui`: campo de nombre + botones Guardar / Cargar / lista de presets del directorio.
4. Crear 3–4 presets de demo con carácter distinto (archipiélago bajo, macizo alpino, isla volcánica...).
5. Test: cargar preset → regenerar → hash del heightfield == hash guardado en el propio JSON al crearlo.

### C2 — Minimapas de campos intermedios

1. Utilidad campo→textura GL (normalización + rampa de color simple), en `ui/` o `render/`.
2. Mostrar con `ImGui::Image` en pestaña "Campos": altura, flow accumulation, humedad, biomas (mínimo; energía de oleaje si A1 está hecho).
3. Refrescar al regenerar. Coste cero cuando la pestaña está cerrada.

### C3 — Rendimiento (>60 fps Release)

1. Instrumentar primero: fps y ms/frame del `frame_clock` en ImGui + cronómetro de generación en el log de progreso. **Medir antes de optimizar.**
2. Vuelo a 100 m/s alrededor de la isla: anotar mínimos. Si ≥60 fps, C3 termina aquí.
3. Si no: sospechosos por orden — rebuild de chunks LOD (¿se reconstruyen de más?), draw calls de vegetación (¿culling efectivo?), tamaño de malla de agua. Optimizar solo el cuello medido.
4. Generación: si >30 s en Release, perfilar etapas (LEM, erosión, hidrología ya son O(n) — el sospechoso sería riverscape).

### C4 — Pulido visual final

1. Revisar niebla + cielo degradado + sol en las 4 direcciones y a 3 alturas (suelo, 200 m, 2 km).
2. Skirts de chunks: barrido visual buscando grietas entre niveles de LOD.
3. Ajuste final de paleta con capturas lado a lado.

- Commits: uno por sub-tarea (`feat: presets json`, `feat: minimapas de campos`, `perf: ...`, `fix: ...`).

## B.4 · FASE D — Fase 2: planeta cube-sphere caminable

**Regla:** D0 es bloqueante. Nada de código de planeta sin spec aprobado. Cada sub-hito es una sesión (o varias) con sus propios criterios. Trabajar en rama `fase2-planeta` tras taggear `v1.0-isla`.

### D0 — Spec propio (sesión de diseño con Rodrigo)

Decisiones que el spec debe cerrar (mínimo):
- **Radio del planeta** (compromiso: lo bastante pequeño para recorrerlo, lo bastante grande para que la curvatura no maree; explorar 20–100 km de radio).
- **Proyección cube-sphere** (con corrección tangencial para uniformizar celdas) y estructura de parches: quadtree por cara, 6 caras.
- **Precisión numérica**: posiciones de mundo en `double` en CPU + render relativo a la cámara (origin rebasing). Criterio: sin jitter a nivel de superficie.
- **Reutilización de campos**: ruido, continentalidad y clima ya están diseñados para muestrear posiciones 3D (restricción del spec de Fase 1) — confirmar que las implementaciones actuales lo cumplen o adaptarlas.
- **Relieve**: LEM/erosión **por parches** locales (la global sobre esfera entera es inviable); definir tamaño de parche, solape y cómo se cosen los bordes.
- **Hidrología en esfera**: alcance realista (¿ríos solo intra-parche? ¿enrutado entre parches?). Esta es la decisión más difícil: dimensionarla honestamente.
- **Agua global**: nivel del mar esférico, render del océano a escala planetaria.
- ¿Generación GPU para parches en caliente? (única puerta abierta para reabrir el non-goal).

### D1 — Esfera renderizable con LOD

- Cube-sphere con quadtree de parches por cara, skirts, frustum culling, split/merge por distancia.
- Cámara: de vuelo orbital a vuelo de superficie continuo.
- **Criterio:** descender de órbita a 2 m del suelo sin grietas entre parches, sin costuras en aristas del cubo y sin jitter (origin rebasing funcionando). Fps >60 en Release durante el descenso.

### D2 — Campos 3D sobre la esfera

- Continentalidad + ruido base muestreados en posiciones 3D de la esfera. Continentes, océanos y clima latitudinal coherentes.
- **Criterio:** capturas de las 12 aristas y 8 esquinas del cubo sin costura visible; misma seed ⇒ mismo planeta (hash de parches de muestra).

### D3 — Relieve por parches (LEM local)

- Al refinar un parche cerca de la cámara, correr LEM/erosión local con condiciones de borde tomadas del nivel anterior; cachear resultados por parche+seed.
- **Criterio:** caminar 1 km cruzando ≥2 fronteras de parche sin escalones ni costuras; regenerar el mismo parche da idéntico resultado (determinismo por parche).

### D4 — Hidrología (alcance según D0)

- Implementar lo que D0 haya dimensionado (mínimo honesto: red de drenaje intra-parche con desembocadura coherente hacia el mar del parche).
- **Criterio:** los del spec D0 — no inventar aquí.

### D5 — Player en la esfera

- Gravedad radial, orientación del controlador al normal local, andar/correr/saltar sobre parches, transición volar↔caminar en cualquier punto del planeta.
- **Criterio:** dar una vuelta caminando/volando alrededor del planeta sin atravesar el suelo ni perder la orientación.

### D6 — Ambiente

- Cielo/atmósfera con scattering simple (horizonte curvo, atardeceres), estrellas de noche; día/noche por rotación solo si HP-1 lo aprueba.
- **Criterio:** captura del limbo del planeta desde órbita y atardecer desde superficie con horizonte curvo correcto.

## B.5 · FASE E — Release

1. Build Release limpia; comprobar que el exe corre fuera del entorno de desarrollo (DLLs junto al exe o estáticas).
2. README: estado real (tabla §4 de este plan como base), controles, build, 3+ capturas buenas, GIF corto si es fácil.
3. Presets de demo incluidos en `presets/`.
4. Tag: `v1.0-isla` (tras C) / `v2.0-planeta` (tras D).

---
---

# APÉNDICE C — Track TFG: amplificación de DEMs reales con constraints hidrológicas

> Decidido el 2026-07-07. **Documento técnico canónico: `docs/investigacion-amplificacion-dem.md`** (encuadre, estado del arte verificado, gap frente a Schott et al. 2024, datasets, métricas, riesgos). Este apéndice es el resumen operativo para retomar el track sin contexto.

## C.1 Qué es (una frase)

Super-resolución de DEMs reales de 30 m (Copernicus GLO-30) a ~2 m usando el **LEM stream-power de este motor como prior físico**, con la **red de drenaje real impuesta como restricción dura** (stream burning) y **validación hidrológica cuantitativa** contra LiDAR (PNOA MDT02) — el pipeline completo de Aechelon en miniatura: datos reales → base de datos geo-específica → render en tiempo real.

## C.2 Estado (2026-07-07)

| Hito | Estado |
|---|---|
| Decisión de idea (entre 10 candidatas) | ✅ #1 elegida; perfil objetivo Ingeniero de Gráficos → Aechelon |
| Informe de viabilidad | ✅ `docs/investigacion-amplificacion-dem.md` (es la propuesta para el tutor) |
| Lectura crítica de Schott et al. 2024 (SIGGRAPH, el competidor) | ✅ §3.1 del informe; PDF local en `tfg/papers/` (no versionado); su código: `github.com/H-Schott/MultiScaleErosion` |
| **E0 — instrumentación + números de referencia** | ✅ `tfg/e0/` (uv + rasterio + WhiteboxTools, corre en ~40 s); resultados en `tfg/e0/resultados-e0.md` |
| **T1a/T1b — la idea central VALIDADA en sintético** | ✅ (2026-07-07) `tfg/amplify/` + `tfg/t1b/resultados-t1b.md`: F1@10m 0.6038 > bicúbica 0.5985 sin coste de elevación |
| Tutor académico | ❌ **PENDIENTE — es el siguiente paso y bloquea la formalización** (ahora con resultados propios en mano, no solo la idea) |
| T1c, E1 completo en adelante | ❌ pendientes (plan en §7 del informe y C.4.1) |

**Números a batir (E0, Benasque/Posets, GLO-30 bicúbica vs MDT02):** RMSE 8.39 m · NMAD 3.16 m · pendiente −2.0° sistemático · **F1 de red de drenaje 0.52 @ 10 m** (0.81 @ 30 m) · colapso del orden 4 de Strahler. El hueco a explotar es la tolerancia fina.

## C.3 Ejes de novedad (defensa frente a "esto ya existe")

1. LEM geomorfológico **calibrado sobre datos reales** (Braun-Willett implícito; uplift/erodibilidad estimados del DEM), no aproximación gráfica con parámetros estéticos.
2. **Constraint dura de la red observada** (Schott ancla crestas — el complementario; nadie ancla la red).
3. **Validación cuantitativa contra ground truth** (Schott solo mide una métrica interna; la familia DL solo RMSE). Incluye E1b: primera evaluación de Schott 2024 contra terreno real.
4. **Escala por tiles con drenaje coherente entre fronteras** (limitación declarada de Schott: 8192² máx en una GPU) + integración en motor caminable.

## C.4 Orden de trabajo del track (cada punto con su criterio en el informe §7)

1. **T0 — Tutor**: llevar el informe a un tutor de gráficos/GIS. Sin esto el TFG no es formal. No bloquea seguir con T1 como proyecto personal.
2. **T1 — Método v0 (prototipo)**: el corazón técnico. Detalle completo en C.4.1.
3. **T2 — E1/E2** (sintético + hidrológico): el resultado central. Batir a la bicúbica en F1@10m sin empeorar elevación.
4. **T3 — E1b** (Schott como baseline con su código público) y **E3** (real GLO-30 vs MDT02).
5. **T4/T5 — E4 ablaciones + E5 más zonas.**
6. **T6 — E6 render**: importar la celda amplificada al motor (herramienta `tools/dem_prep` CLI con GDAL, fuera del motor) y volarla a >60 fps. La demo Aechelon.
7. **T7 — Memoria y defensa.**

### C.4.1 T1 — Diseño técnico del amplificador v0

Prototipo en Python dentro de `tfg/` (itera en minutos; el port a C++ del motor llega en T6, solo si v0 valida). Reutiliza el pipeline E0 tal cual como instrumentación.

**Algoritmo v0 (cada paso es una pieza medible y ablacionable en E4):**

```
Entrada: Z30 (DEM 30 m recortado, p.ej. GLO-30 o MDT02 degradado)
Salida:  Z2  (DEM 2 m amplificado)

1. BASE       Z2 := bicubic(Z30)                     # el baseline E0 es el punto de partida
2. RED REF    R  := red D8 de Z30 (umbral 1 km²),    # la red observada = constraint
              rasterizada en la malla 2 m (1 celda)
3. UPLIFT     U(p) := K·A(p)^m · S(p)^n              # SPL invertida: equilibrio local
              con A, S del DEM grueso remuestreados;  # (m/n ≈ 0.45; U normalizado)
4. ERODIB.    K(p) := K0 · (1 + ruido multiescala)   # el detalle EMERGE del proceso
                                                      # (filosofía A.5: nada estampado)
5. LEM FINO   N iteraciones Braun-Willett implícito  # numpy: orden topológico por
              sobre Z2 con U y K(p);                  # receptores; N pequeño (decenas)
              la red R va QUEMADA: sus celdas se      # stream burning como condición,
              fijan/deprimen ε como condición de     # no como zanja estética
              contorno interna; borde del tile = BC
6. SUAVIZADO  breaching multi-escala sobre la        # idea adoptada de Schott 2024:
   DE ZANJA   trinchera de R (radios decrecientes)   # sin cañones de 1 celda
7. ANCLAJE    Z2 += blur_σ(upsample(Z30) − Z2)       # re-anclaje de baja frecuencia
   LF         con σ ≈ 30-60 m                         # (retargeting difusivo): la
                                                      # macro-forma es EXACTAMENTE la real
8. VALIDAR    pipeline E0 completo (elevación +      # mismas métricas, mismos umbrales
              derivados + F1 de red + Strahler)
```

**Subfases con criterio de salida:**

| Subfase | Contenido | Criterio de salida |
|---|---|---|
| T1a | ✅ (2026-07-07) Refactor a `tfg/valida/` + **fix de reproducibilidad**: WhiteboxTools a 1 hilo (el multihilo hacía bailar el F1 en el 3er decimal) | E0 re-ejecuta idéntico dos veces ✓ |
| T1b | ✅ (2026-07-07, 6 intentos — `tfg/t1b/resultados-t1b.md`) LEM numpy (pointer doubling + niveles, sin numba) + red con **valley snapping densificado** como Dirichlet + U invertido sin normalizar + solo evolucionan canales (A₀ Flint 22 500 m²) | **F1@10m 0.6038 > 0.5985 bicúbica, con RMSE intacto (2.04 vs 2.01)** ✓ |
| T1c | Detalle emergente + anclaje LF (pasos 4, 6, 7) — el punto de partida ya NO empeora elevación, así que el listón es mejorar ambas cosas | RMSE/NMAD ≤ bicúbica Y F1@10m > bicúbica |
| T1d | E1 sintético completo + tabla comparativa | Tabla método vs bicúbica en las ~12 métricas |

**Kill-criteria (honestidad ante todo):** si tras ~2 semanas de iteración T1b no consigue F1@10m > 0.52 (la bicúbica) en el caso sintético, la constraint no está funcionando: parar, diagnosticar con dumps (¿la red quemada se desancla en el LEM? ¿el umbral de área difiere entre escalas?) y solo después replantear el método. Si T1c no logra detalle sin empeorar NMAD, el fallback es degradar el LEM a generador del residuo de alta frecuencia (menos ambicioso pero publicable igualmente por los ejes 2-3).

**Decisión de librería LEM en Python:** primero intentar implementación propia numpy (el algoritmo Braun-Willett ya está entendido e implementado en C++ en `terrain/lem.cpp` — portarlo es 1 sesión y da control total sobre el burning); `fastscapelib` como referencia de contraste, no como dependencia.

## C.5 Operativa del track

- Proyecto Python: `cd tfg` → `uv sync` → `uv run python e0/run_e0.py`. Deps: rasterio, numpy, scipy, whitebox, matplotlib. Datos en `tfg/e0/data/` (gitignored); cómo re-descargarlos (URLs y flujo CNIG con secuenciales) está en `tfg/e0/resultados-e0.md` §Reproducir.
- El tile GLO-30 usado es la edición AWS 2022 (anterior al relleno de alta resolución 2023 en Pirineos — sin contaminación). Ojo al gap DSM/DTM (los globales miden copas; PNOA mide suelo): tratado en el informe §4.1.
- Ritmo recomendado: el track TFG se abre en serio **tras cerrar la Fase C** del motor (`v1.0-isla`), en rama `tfg-amplificacion`. E0 ya demostró que la instrumentación no depende del motor, así que T0-T2 pueden avanzar en paralelo si conviene.

## C.6 Calendario orientativo integrado (motor + TFG, curso 2026-27)

El mayor riesgo del año no es el motor (A2 tiene fallback documentado): es que **la idea central del TFG no funcione**. Fail-fast global ⇒ T1 se valida ANTES de invertir el otoño en él.

| Cuándo | Motor | TFG |
|---|---|---|
| Jul 2026 | A2 oxbows → Fase A cerrada | **T1 v0 (validar la idea YA)** + T1d |
| Ago 2026 | Fases B + C → tag `v1.0-isla` (+ E release isla si apetece) | E1b (Schott baseline) |
| Sept 2026 | — (arranque de curso) | **T0 tutor con informe + números de T1/E1b en mano** (propuesta mucho más fuerte que solo la idea) |
| Oct-Nov 2026 | Fase D0 (spec planeta) en paralelo si hay hueco | E3 real + E4 ablaciones |
| Dic 2026-Feb 2027 | Fase D1-D3 según carga del TFG | E5 zonas + T6 render demo (port C++) |
| Mar-Jun 2027 | Fase D continúa o pausa | **T7 memoria + defensa** |

Regla del calendario: ante conflicto de tiempo, **gana el TFG** (es lo que tiene fecha de entrega y lo que abre la puerta de Aechelon); el planeta (Fase D) es el colchón flexible.

---

> *Hiper Plan — artefacto vivo. Al cerrar cada fase, actualizar la tabla §4 (estado) y marcar los criterios §7. Si una decisión cambia (open questions), editar aquí, no en la memoria de un modelo.*
