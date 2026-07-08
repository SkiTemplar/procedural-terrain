# Procedural Terrain — Plan Maestro (Hiper Plan)

> **Generado:** 2026-07-06 · modo HIGH · skill hiper-plans
> **Canónico:** `docs/plan-maestro.md` (este fichero, en el repo) · copia en `~/.ultron/plans/2026-07-06-procedural-terrain-macro-plan.md`
> **Estado:** ACTIVO
> **Propósito:** que cualquier persona o agente (Sonnet, Opus, humano) pueda continuar el proyecto **sin contexto previo de conversación**. Todo lo necesario está aquí o enlazado desde aquí.

> **Actualización 2026-07-08 (2) — NATURALIDAD:** decisiones razonadas de Rodrigo tras `docs/investigacion-naturalidad.md`: (1) **A2 oxbows DESCARTADO por diseño** — en esta isla los ríos fluyen por relieve montañoso, no por llanuras aluviales blandas tipo Amazonas donde se forman los meandros; Agua v2 queda **cerrada en 8/9**. (2) Entra el track de naturalidad: **N1** picos de un vértice (diagnóstico+fix), **N2** costa emergente del LEM (opción B del informe: la silueta y los islotes emergen del contorno h=0 del relieve, fuera el disco radial y el anillo satélite estampado), y **C4 ampliado** con luz (sombras CSM + AO + tonemapping + cielo) y agua (profundidad real + espuma por `wave_energy` + flujo D8 + oleaje direccional). Orden: N1 → N2 → B → C (C4 ampliado) → E.

> **Actualización 2026-07-08 — REFOCUS (sustituye a la nota TFG del 2026-07-07):** este proyecto pasa a ser un **proyectín personal**: un mundo generado proceduralmente **realista y lo más natural posible**, sin framing de portfolio Aechelon/Shield AI. Decisiones de Rodrigo (2026-07-08): (1) el track de amplificación de DEMs queda **DESCARTADO como TFG** — el TFG saldrá de un problema que Rodrigo encuentre e investigue él mismo, en un proyecto futuro aparte; la carpeta `tfg/` y sus resultados (E0, T1b) se conservan como experimento archivado (Apéndice C, tombstone). (2) El alcance final es la **isla v1.0**: Fases A2 → B → C → E y cierre; la Fase D (planeta cube-sphere) queda **descartada** (tombstone en B.4). (3) Rige el **Protocolo Socrático** (`~/.claude/CLAUDE.md`): la IA escribe el código; las decisiones no triviales de arquitectura/resolución las toma Rodrigo.

---

## 0. CÓMO RETOMAR ESTE PROYECTO SIN CONTEXTO (léeme primero)

Si estás leyendo esto sin acceso a las conversaciones previas (otro modelo, otra sesión, otro humano), este es el orden de arranque:

1. **Lee este plan entero.** Es el artefacto canónico: estado, fases, criterios y manual de operaciones. Ante conflicto entre este plan y el README, **gana este plan**.
2. **Documentos de soporte, en este orden:**
   - `docs/superpowers/specs/2026-07-02-procedural-terrain-design.md` — el diseño aprobado del juego/motor.
   - `docs/investigacion-agua-v2.md` — algoritmos y fuentes de los 9 pasos de agua (8 ✅; 9 oxbows ❌ descartado por diseño 2026-07-08).
   - `docs/investigacion-naturalidad.md` — diagnóstico y opciones elegidas del track de naturalidad (N1 picos, N2 costa emergente, C4 luz+agua).
   - `docs/investigacion-lem.md` — trasfondo del LEM si tocas `terrain/lem`.
   - (📦 archivado 2026-07-08, ya no es parte del proyecto) `docs/investigacion-amplificacion-dem.md` y `tfg/` — el antiguo track TFG; solo consultar si algún día se retoma como proyecto aparte.
3. **Estado a 2026-07-08:** Fase A **CERRADA en 8/9** (A1 ✅; A2 oxbows descartado por diseño). Siguiente: **N1 picos de un vértice** → N2 costa emergente → B → C (C4 ampliado con luz+agua) → E y cierre en `v1.0-isla`. El track TFG está descartado (ver notas de cabecera).
4. **Comandos esenciales (desde la raíz del repo, en PowerShell):**
   - Compilar + tests: `tools\build.ps1 -Test` (recarga vcvars64 siempre; ver A.1).
   - Ejecutar: `cmake-build-debug-opengl\ProceduralTerrain.exe [seed [x z yaw pitch [alt]]]` (pose de cámara para capturas; ver A.3).
   - Dump de verificación sin ventana: `ProceduralTerrain.exe --dump-costa <prefijo> [seed]` → BMPs de materiales y oleaje.
   - Captura de pantalla: `tools\capture.ps1 -Out foto.png`.
5. **Reglas de oro que NO se negocian:** "no fakear nada" (A.5), determinismo por seed (los tests lo protegen), tests verdes antes de commit, verificación visual de todo cambio de render/terreno (A.3), commits pequeños `tipo: descripción` en minúsculas.
6. **Objetivo del autor (refocus 2026-07-08):** proyectín personal — que el mundo sea **lo más natural y realista posible** y el proyecto quede **cerrado y shippeable** (isla v1.0). No hay agenda de portfolio: al priorizar, pesa lo que hace el mundo más creíble y el cierre más limpio. Rige el Protocolo Socrático: la IA escribe el código; la arquitectura y la resolución de problemas no triviales las decide Rodrigo (opciones + trade-offs, elección razonada).

---

## 1. PROBLEM

El proyecto tiene el diseño aprobado (`docs/superpowers/specs/2026-07-02-procedural-terrain-design.md`) y ~70 % de la Fase 1 implementada, pero el conocimiento de *qué queda, en qué orden y con qué criterios de aceptación* vive disperso entre la conversación con el modelo, el historial de git y dos informes de investigación. Si se pierde el acceso al modelo actual, se pierde el rumbo. Este documento consolida el objetivo final y las fases restantes en un artefacto ejecutable y persistente.

**Objetivo final del proyecto (refocus 2026-07-08):** un juego de exploración en primera persona sobre una **isla procedural de 10×10 km realista y natural**, con relieve, ríos, lagos, costas y biomas que **emergen de simulación física** (LEM stream-power, erosión, hidrología) — nunca estampados. Estética low-poly flat-shaded, cero assets externos, todo generado por código y determinista por seed. No hay combate ni mecánicas: **el terreno es el juego**. El proyecto se cierra en `v1.0-isla`; el planeta cube-sphere (antigua Fase 2) quedó descartado.

## 2. NON-GOALS

- Multijugador, guardado de partida, gameplay más allá de explorar.
- Texturas realistas / PBR / normal maps — el flat shading por material es decisión estética cerrada.
- Assets externos (modelos, texturas, sonidos importados). Todo por código.
- Streaming infinito de chunks (el mundo es finito: la isla).
- Generación en GPU (compute). El pipeline es CPU, offline, determinista.
- Migrar de OpenGL a Vulkan/DX12.
- Editor de mundo. El panel ImGui + presets JSON es toda la herramienta.
- **Planeta cube-sphere caminable (antigua Fase 2/D)** — descartado 2026-07-08; tombstone en B.4.
- **TFG / portfolio Aechelon-Shield AI** — fuera de este proyecto desde 2026-07-08; el antiguo track queda archivado (Apéndice C).

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
| TFG (📦 archivado) | `tfg/` (proyecto uv: e0/, amplify/, t1b/, papers/) | 📦 Archivado 2026-07-08 — E0 y T1b hechos; ya no es parte del proyecto (Apéndice C) |
| Vegetación | `veg/models` (pino, frondoso, roca, mata, arbusto, tronco caído), `veg/veg_renderer` | ✅ instanciado, culling por celdas de 512 m, colocación determinista pisando el suelo. Pendiente: auditoría de densidades/variedad por bioma |
| Player | `player/controller` | ✅ andar/volar completo (F alterna, colisión de suelo) |
| UI | `ui/debug_ui` | ✅ sliders en vivo + regenerar. ❌ sin presets JSON, ❌ sin minimapas de campos |
| Tests | `tests/terrain_tests.cpp` | ✅ existen (hash de seed, invariantes de hidrología) |

**Nota histórica:** el Hito 3 original (placas tectónicas Voronoi) fue **sustituido** por el LEM stream-power como esqueleto del relieve (commit `233cf00` "fuera placas"). No reimplementar placas: decisión cerrada.

**Estado del README:** desactualizado respecto al código (dice Hito 5 pendiente; la vegetación ya está mayormente implementada). Fiarse de esta tabla, no del README.

## 5. MVP

**MVP del plan = la isla cerrada y shippeable:** isla 10×10 km con Agua v2 cerrada en 8/9 (oxbows descartados por diseño), costa e islotes emergentes (N2), sin picos artificiales (N1), luz y agua del C4 ampliado, vegetación por biomas auditada, presets JSON con 3–4 mundos de demo, minimapas de depuración y >60 fps en Release. Eso es un juego de exploración completo por sí mismo — y desde el refocus 2026-07-08 el MVP **es** el proyecto entero: no hay Fase 2.

## 6. FAIL-FAST ORDER — mapa de fases

Ordenadas por dependencia y riesgo. **El detalle ejecutable de cada fase está en el Apéndice B.**

| # | Fase | Contenido | Riesgo | ETA (sesiones) |
|---|---|---|---|---|
| A | Agua v2 — cierre | ✅ **CERRADA 2026-07-08 en 8/9**: A1 playas por oleaje ✅ · A2 oxbows ❌ descartado por diseño (ríos de montaña, no llanura aluvial) | — | — |
| N1 | Picos de un vértice **← SIGUIENTE** | Diagnóstico H1-H3 (`investigacion-naturalidad.md` §3, empezar por `detail_amplitude_m=0`) + fix del culpable real | BAJO | 1–2 |
| N2 | Costa emergente del LEM | Opción B del informe §1: continentalidad → prior débil de uplift, LEM con fondo marino real, costa = contorno h=0; islotes skerries emergentes (fuera el anillo satélite estampado) | **MEDIO-ALTO** (regenera todos los mundos, hash de tests cambia) | 2–4 |
| B | Vegetación — auditoría | Densidades/variedad por bioma, uso real de los 6 modelos, transiciones (sobre el mundo ya final de N2) | BAJO | 1 |
| C | Hito 6 — pulido | C1 presets JSON · C2 minimapas · C3 rendimiento >60 fps · **C4 AMPLIADO**: luz (L1 sombras CSM, L2 AO por vértice, L3 tonemapping, L4 cielo) + agua (W1 profundidad, W2 espuma por `wave_energy`, W3 flujo D8 en ríos, W4 Gerstner direccional) | MEDIO (C3, L1) | 7–10 |
| E | Release | Build Release empaquetada, README final, capturas, presets de demo | BAJO | 1 |

(La antigua Fase D — planeta — está descartada desde 2026-07-08; tombstone en B.4.)

Reglas de tránsito entre fases:
- No se abre una fase con la anterior en rojo (tests o criterios de la §7 sin cumplir).
- **Al cerrar C: crear tag `v1.0-isla`.**
- E se ejecuta tras C y **cierra el proyecto**.

## 7. SUCCESS CRITERIA (checkeables)

**Fase A (cerrada 8/9):**
- [x] Captura de costa: se distinguen ≥3 tipos de orilla (playa disipativa ancha, grava/acantilado expuesto, marisma/roca abrigada) correlacionados con el mapa de energía de oleaje (visible en minimapa o dump). **Cumplido 2026-07-07**: dump por octantes (E 0.57 barlovento NO vs 0.15 sotavento), 4 regímenes presentes (arena 112k / grava 18k / marisma 71k celdas) y captura 3D de costa sur con disipativa+marisma.
- ~~Oxbows (paso 9)~~ **DESCARTADO por diseño 2026-07-08**: los meandros exigen llanura aluvial de pendiente ~nula y orillas blandas; los ríos de esta isla son cortos y de montaña.

**Fase N1:**
- [ ] Culpable identificado con evidencia (H1/H2/H3 del informe §3, capturas antes/después).
- [ ] Contador de picos (celda > máx. de sus 8 vecinos + umbral talus) ≈ 0 tras el fix, sin planchar crestas legítimas (captura de la misma cumbre antes/después).
- [ ] `ctest` verde (hash regenerado solo si el fix toca generación, con justificación en el commit).

**Fase N2:**
- [ ] La silueta y los islotes salen del contorno h=0 del relieve LEM: sin `max()` de anillo satélite ni smoothstep radial como forma dominante (solo prior débil + condición de borde).
- [ ] Captura aérea: bahías/rías donde hay valles, cabos donde hay divisorias, ≥2 islotes/skerries emergentes de tamaño no uniforme.
- [ ] El mar sigue rodeando el dominio (invariante isla) y los 4 regímenes de orilla de A1 sobreviven.
- [ ] `ctest` verde con hash regenerado una sola vez y justificado.

**Fase B:**
- [ ] Los 6 modelos de `veg/models.h` aparecen en el mundo (verificable con contador por tipo en el panel ImGui).
- [ ] Densidad respeta bioma: sin árboles en arena/roca/nieve/agua; matas en pradera; troncos caídos solo en bosque.

**Fase C:**
- [ ] Botones ImGui Guardar/Cargar preset escriben/leen `presets/*.json`; cargar un preset y regenerar reproduce el mundo (mismo hash de heightfield que al guardarlo).
- [ ] ≥4 minimapas de campos intermedios visibles en ImGui (altura, flow accumulation, humedad, biomas).
- [ ] En Release, cámara volando a 100 m/s sobre la isla: fps mínimo ≥60 (medido con el frame_clock, mostrado en ImGui).
- [ ] Generación 4096² en Release ≤30 s (cronometrada en el log de progreso).

**Fase E:**
- [ ] `cmake --build cmake-build-release-opengl` limpio; el exe corre en una máquina sin vcpkg (deps estáticas o DLLs junto al exe).
- [ ] README actualizado con estado real, controles y 3+ capturas.

## 8. ROLLBACK

- Commits pequeños por sub-tarea (convención existente: `feat:`/`fix:` + descripción en minúsculas). Cada sub-tarea de apéndice = 1–2 commits como máximo.
- Fallo en una sub-tarea ⇒ `git revert <commit>` (preferido) o `git reset --hard HEAD~1` si aún no se ha empujado nada encima.
- **N2 (costa emergente) es el cambio de generación más gordo:** commits pequeños por sub-paso (prior de uplift → fondo marino → islotes), captura de verificación en cada uno, y el hash del test se regenera UNA vez al final con justificación. Si el resultado no convence tras 2 intentos de tuning, revert y se replantea con dumps.
- Al cerrar la Fase C: `git tag v1.0-isla && git push --tags` — la isla estable en `main` es el estado final del proyecto.
- No hay BD ni estado externo: el rollback es siempre puro git.

## 9. TOKEN BUDGET (si ejecuta un agente)

| Fase | Estimación |
|---|---|
| A | ✅ cerrada |
| N1 | 20–40 K (diagnóstico instrumentado + fix + capturas) |
| N2 | 80–150 K (iteración visual fuerte: la costa nueva se tunea mirando) |
| B | 20–30 K |
| C | 90–150 K (C4 ampliado: luz + agua, con sus ciclos de captura) |
| E | 15–25 K |

Referencia de modo: MEDIUM ~3 K · HIGH ~8 K · ULTRA ~15 K por turno de planificación. La iteración visual (captura → ajuste → captura) es lo que dispara el gasto: presupuestar 3–5 ciclos por rasgo visual nuevo.

## 10. VERIFICATION QUESTIONS (auto-chequeo CoVe)

- [x] ¿El plan asume algo que no está en INPUTS? — No: el estado de código se verificó fichero a fichero el 2026-07-06 (tabla §4). El único supuesto es que los tests actuales están verdes; el primer paso de la Fase A es comprobarlo.
- [x] ¿Cada criterio de §7 es checkeable y no interpretable? — Sí: comandos, contadores, hashes, fps medidos y capturas con rasgo concreto que buscar.
- [x] ¿El ROLLBACK cubre el peor caso? — Sí: el peor caso real es A2 (descarte documentado tras 2 intentos).
- [x] ¿FAIL-FAST ataca primero el mayor bloqueo? — Sí: A2 es lo más arriesgado pendiente.
- [x] ¿Algún paso exige decisión humana que no esté en OPEN QUESTIONS? — Las decisiones de alcance quedaron cerradas el 2026-07-08 (refocus); el resto tiene defaults.
- [x] ¿Los NON-GOALS son realmente out-of-scope? — Sí: todos vienen del spec aprobado o del refocus 2026-07-08.
- [x] ¿El TOKEN BUDGET es consistente con el modo y las ETAs? — Sí: escala con las sesiones estimadas y con el coste conocido de la iteración visual.

## 11. OPEN QUESTIONS

| ID | Pregunta | Default si no hay respuesta | Owner |
|---|---|---|---|
| HP-1 | ¿El objetivo final añade algo más al "explorar" (foto-modo, ciclo día/noche, puntos de interés)? | No. Explorar es el juego. Stretch post-v1.0 solo si apetece | Rodrigo |
| ~~HP-2~~ | ~~¿Se mantiene ~sept 2026 como arranque de Fase 2?~~ | **CERRADA 2026-07-08**: no hay Fase 2 (planeta descartado) | — |
| ~~HP-3~~ | ~~¿Oxbows (A2) compensa su riesgo?~~ | **CERRADA 2026-07-08**: descartado por diseño — ríos de montaña, no llanura aluvial | — |
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
- Todo en `main`; el proyecto se cierra taggeando `v1.0-isla`.

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

### A2 — Oxbows / meandros (paso 9) — ❌ DESCARTADO por diseño (2026-07-08)

Decisión razonada de Rodrigo: los oxbows exigen llanura aluvial ancha, de pendiente ~nula y orillas blandas (tipo Amazonas); los ríos de esta isla son cortos y de montaña — estampar meandros aquí sería exactamente el "fakeo" que prohíbe A.5. Agua v2 queda cerrada en **8/9**. El detalle técnico que había (Howard-Knutson, cutoffs, `aparis69/Meandering-rivers`) queda en el historial de git de este fichero por si un futuro mundo de llanura lo justificara.

## B.1b · FASES N1 y N2 — Naturalidad (detalle en `docs/investigacion-naturalidad.md`)

### N1 — Picos de un vértice (diagnóstico ANTES de fix)

1. Instrumentar contador de picos sobre el heightfield (celda > máx. de sus 8 vecinos + umbral talus) y localizarlos (¿roca? ¿crestas? ¿conos de deyección?).
2. A/B barato: `detail_amplitude_m = 0` (H1, panel ImGui, sin recompilar) → si desaparecen, el culpable es el ruido de detalle del mesher; si no, probar sin `sharpen_crags` (H2) y mirar el contador pre-mesher (H3, gotas).
3. Fix según culpable (informe §3): H1 → grano fino a color/normal o amortiguado por curvatura; H2 → suavizar el borde del banco; H3 → depósito 3×3 como ya hace la erosión.
- Commits: `fix: ...` según culpable. Verificación: capturas de la misma cumbre antes/después + contador ≈ 0.

### N2 — Costa emergente del LEM (opción B del informe §1)

1. **Prior débil**: `coast_at` deja de devolver LA forma — el smoothstep radial pasa a prior de uplift (con sus lóbulos y el eje tectónico) y condición de borde (mar garantizado en el perímetro).
2. **Fondo marino real**: el dominio del LEM se extiende bajo el nivel del mar (la caída analítica `h −= drop·sea_depth` desaparece o queda residual).
3. **Costa = contorno h=0** tras LEM+erosión: adaptar `shape_coast`/flatten para operar sobre la costa emergente.
4. **Islotes skerries**: retirar el bloque `satellite` estampado (generator.cpp:62-68); los islotes deben emerger como picos del prior que asoman. Si no asoman, se tunea el prior, no se estampan.
5. Re-validar A1 (4 regímenes de orilla sobreviven), capturas aéreas + `--dump-costa`, hash del test regenerado UNA vez con justificación.
- Commits pequeños por sub-paso. Es el cambio de generación más gordo del track: cada paso con su captura.

### C4 ampliado — luz y agua (se ejecuta dentro de la Fase C)

- Luz: L1 sombras CSM (2-3 cascadas) · L2 AO por vértice precomputado del heightfield · L3 tonemapping+gamma · L4 cielo gradiente + niebla con altura.
- Agua: W1 color por profundidad real del fondo · W2 espuma de orilla ∝ `wave_energy` · W3 ríos con flujo D8 (olas a lo largo del cauce, amplitud ∝ pendiente) · W4 mar Gerstner orientado con la rosa de vientos.
- Regla de oro: cada efecto visual se ancla a un campo físico ya simulado — nada decorativo desacoplado del proceso.

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

## B.4 · FASE D — Fase 2: planeta cube-sphere — ❌ DESCARTADA (2026-07-08)

Decisión del refocus 2026-07-08: el proyecto se cierra como isla (`v1.0-isla`), sin fase de planeta. El detalle completo de la fase (D0 spec bloqueante, D1 cube-sphere+LOD, D2 campos 3D, D3 LEM por parches, D4 hidrología, D5 player esférico, D6 atmósfera) queda en el historial de git de este fichero (estado a commit `74a63fc`), por si algún día se retomara como proyecto aparte — esa decisión no está tomada.

## B.5 · FASE E — Release

1. Build Release limpia; comprobar que el exe corre fuera del entorno de desarrollo (DLLs junto al exe o estáticas).
2. README: estado real (tabla §4 de este plan como base), controles, build, 3+ capturas buenas, GIF corto si es fácil.
3. Presets de demo incluidos en `presets/`.
4. Tag: `v1.0-isla` — el tag de cierre del proyecto.

---
---

# APÉNDICE C — Track TFG: amplificación de DEMs — 📦 ARCHIVADO (2026-07-08)

> **Descartado como TFG por decisión de Rodrigo (2026-07-08).** El TFG saldrá de un problema que Rodrigo encuentre e investigue **él mismo**, en un proyecto futuro aparte de este repo. Regla asociada (Protocolo Socrático): la IA podrá señalar problemas de dominio, **nunca la solución**.

Lo que queda archivado (se conserva en el repo, no se trabaja):

- `tfg/` — proyecto uv con **E0** (instrumentación + números de referencia: `tfg/e0/resultados-e0.md`) y **T1a/T1b** (amplificador v0 validado en sintético: F1@10m 0.6038 > 0.5985 bicúbica con RMSE intacto; `tfg/t1b/resultados-t1b.md`).
- `docs/investigacion-amplificacion-dem.md` — informe de viabilidad completo (estado del arte, gap vs Schott 2024, datasets, métricas).
- El detalle operativo del track (C.1–C.6: método v0, kill-criteria, calendario integrado) está en el historial de git de este fichero (estado a commit `74a63fc`).

Si un proyecto futuro retomara esta línea, este material es un punto de partida posible — pero esa decisión no está tomada y no pertenece a este plan.

---

> *Hiper Plan — artefacto vivo. Al cerrar cada fase, actualizar la tabla §4 (estado) y marcar los criterios §7. Si una decisión cambia (open questions), editar aquí, no en la memoria de un modelo.*
