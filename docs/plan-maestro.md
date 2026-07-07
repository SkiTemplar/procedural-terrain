# Procedural Terrain — Plan Maestro (Hiper Plan)

> **Generado:** 2026-07-06 · modo HIGH · skill hiper-plans
> **Canónico:** `docs/plan-maestro.md` (este fichero, en el repo) · copia en `~/.ultron/plans/2026-07-06-procedural-terrain-macro-plan.md`
> **Estado:** ACTIVO
> **Propósito:** que cualquier persona o agente (Sonnet, Opus, humano) pueda continuar el proyecto **sin contexto previo de conversación**. Todo lo necesario está aquí o enlazado desde aquí.

> **Actualización 2026-07-07 — Track TFG decidido:** el trabajo del año / candidato a TFG es la **amplificación de DEMs reales con constraints hidrológicas** (pipeline estilo Aechelon: DEM real → LEM como prior físico → render en tiempo real). Informe de viabilidad, estado del arte, datasets y plan de experimentos en `docs/investigacion-amplificacion-dem.md`. Consecuencias para este plan: las Fases A–C siguen intactas (son el motor que el TFG reutiliza) y se cierran antes de abrir el track; el track TFG irá en rama propia tras `v1.0-isla`; la Fase 2 gana la opción de ser "la Tierra real caminable" (decisión en D0).

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
| Terreno | `terrain/noise`, `heightfield`, `lem`, `erosion`, `hydrology`, `riverscape`, `generator`, `terrain_config`, `detail`, `mesher`, `chunked_terrain` | ✅ mundo 10×10 km, heightfield 4096² (~2.4 m/celda), LEM O(n) Braun-Willett + uplift anisótropo, erosión de gotas, priority-flood + D8, clima regional. **LOD por chunks ya existe** |
| Agua v2 | `riverscape.*` principalmente | ✅ pasos 1–7 de 9 (efluentes de lagos, Leopold & Maddock, lecho monótono, cascadas y rápidos, confluencias, deltas, islotes fluviales). ❌ pasos 8–9 pendientes |
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
| A | Agua v2 — cierre | A1 playas por energía de oleaje (paso 8) · A2 oxbows (paso 9) | A2 **ALTO** (muta la traza en planta, puede romper el D8) | 2–3 |
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
- [ ] Captura de costa: se distinguen ≥3 tipos de orilla (playa disipativa ancha, grava/acantilado expuesto, marisma/roca abrigada) correlacionados con el mapa de energía de oleaje (visible en minimapa o dump).
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

```powershell
# consola con vcvars64 cargado
cmake --preset Debug-OpenGL
cmake --build cmake-build-debug-opengl
cmake-build-debug-opengl\ProceduralTerrain.exe

# Release (para medir rendimiento SIEMPRE en Release)
cmake --preset Release-OpenGL
cmake --build cmake-build-release-opengl
```

- **Matar el exe antes de recompilar** (el linker falla si el binario está en uso).

## A.2 Tests

```powershell
cmake --build cmake-build-debug-opengl
ctest --test-dir cmake-build-debug-opengl --output-on-failure
```

Invariantes que protegen: misma seed ⇒ mismo hash de heightmap; tras priority-flood no quedan sumideros interiores; todo río alcanza mar o lago. **Cualquier cambio en `terrain/` debe dejar esto verde o actualizar el test con justificación.**

## A.3 Verificación visual (obligatoria para todo cambio de render/terreno)

El render no se testea con píxeles: se mira. Flujo probado:
1. Lanzar el exe (en Debug, la generación 4096² tarda **~150 s**; esperar a que la ventana muestre el mundo).
2. Capturar pantalla, p. ej. con .NET desde PowerShell (`System.Drawing`, `Graphics.CopyFromScreen`) a una PNG del scratchpad.
3. Abrir/inspeccionar la PNG y comparar contra el criterio de aceptación de la sub-tarea (§7 / Apéndice B).
4. Iterar. Presupuestar 3–5 ciclos por rasgo visual nuevo.

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

### A1 — Variedad de playa por energía de oleaje (paso 8) — riesgo MEDIO

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

> *Hiper Plan — artefacto vivo. Al cerrar cada fase, actualizar la tabla §4 (estado) y marcar los criterios §7. Si una decisión cambia (open questions), editar aquí, no en la memoria de un modelo.*
