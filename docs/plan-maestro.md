# Procedural Terrain — Plan Maestro (Hiper Plan)

> **Generado:** 2026-07-06 · modo HIGH · skill hiper-plans
> **Canónico:** `docs/plan-maestro.md` (este fichero, en el repo) · copia en `~/.ultron/plans/2026-07-06-procedural-terrain-macro-plan.md`
> **Estado:** ACTIVO
> **Propósito:** que cualquier persona o agente (Sonnet, Opus, humano) pueda continuar el proyecto **sin contexto previo de conversación**. Todo lo necesario está aquí o enlazado desde aquí.

> **SESIÓN 2026-10-29 — EROSIÓN MARINA DE LA COSTA ROCOSA (auditoría 2026-10 `agua-costa.md` #5, rama `wf/costa-sunamura`):** nueva etapa `erode_rocky_coast` (`coast.cpp`) entre `compute_coast_exposure` y `classify_materials`, seguida de una segunda exposición. Retroceso de acantilado de **Sunamura**: el frente avanza `k·ln(F_w/F_r)` por paso (240 pasos) solo si `F_w > F_r`, con `F_w = √E·exp(−W/L)` (la ola pierde fuerza al cruzar la plataforma que deja el propio retroceso, L = 150 m) y `F_r` = litología (×2.5 entre la formación más blanda y la más dura) × diaclasado (fbm λ ~40 m: el ruido solo modula la resistencia). Cobertura de playa por arena fluvial con alcance σ ∝ √Q de cada boca (no se usa `river_supply`, que satura a 1 en el 90 % de la costa). El agua dulce y el surco de las desembocaduras (caudal de cauce sin lámina) no se socavan. La F_0 de cada celda es la de su **tramo de costa original** (BFS 4-conexo desde la costa): la ola solo aporta la anchura de plataforma cruzada, así que el oleaje de un cabo no se filtra de lado a las calas abrigadas. Las celdas socavadas bajan a una plataforma de −0.5 a −2 m (1:100) con rampa; toda la etapa solo excava (`min`). **Emerge**: plataforma más ancha con más oleaje y menos dureza, festoneado de cabos y calas por diaclasado, farallones de roca más resistente, frente de ~50° a escala de celda. Dump (seed por defecto, sin etapa → con etapa, tras la review del 29-10): pies de acantilado 6 → 523; plataforma 0.15 → 13.5 ha (anchura media 38 m); farallones 3 → 19 (pequeños, ninguno > 3 m: la costa expuesta de esta seed es una llanura baja aplanada por `shape_coast`). **Criterio anchura–E**: se mide en el log de la etapa con la anchura SOCAVADA delante de cada celda en pie frente a la E previa de su tramo (sin batimetría natural): tramos socavados +0.37 y pies con plataforma socavada +0.41 (seeds 99/7/4242: +0.60/+0.55/+0.35 y +0.52/+0.15/+0.29). La línea del dump «fondo somero al pie–E» mide plataforma natural + abrasión y su signo depende de la seed (−0.22 en la de referencia): no es el criterio. Sanidad N0, perchado y río seco sin cambios. Imagen nueva `-litoral.bmp` (calado del fondo somero). Golden actualizado. **Pendiente del ojo de Rodrigo** (dientes de sierra en el flanco N del skerry NO; acantilados bajos por el aplanado previo de costa).

> **SESIÓN 2026-07-13 (3) — REVIEW EN VIVO de Rodrigo, agua+ríos (commits `a17da10`…`121ce21`):** su diagnóstico ("agua en un plato llano, sin recipiente") era correcto: la lámina se asentaba `bed0 + calado` SOBRE el terreno y el único cuenco era el socavón de 0.35 m → **FRANCOBORDO** (`kFreeboard 0.45`, hydrology paso 2): la lámina nace bajo el terreno original y el río va DENTRO de su cauce — perchado 31.5k→17.6k (costero 10k→2.6k). **Delta-tapón**: el lóbulo rellenaba el canal activo hasta sea+0.35 y el río moría en seco contra su propio delta (contador nuevo "río seco": 683 celdas, peor un caudal 1.47M en la boca SW) → el lóbulo ya no deposita sobre celdas con caudal ≥ umbral → **río seco 0**. **Barras**: el núcleo elíptico recto sellaba el canal en curvas → nunca pisa el thalweg (barras laterales, como las reales). Shader: paredes de agua del borde de cinta desvanecidas (salvo cascadas whitewater), fresnel escalado por calado, turquesa somero apagado, moiré del mar roto con fase modulada por ruido, tren corto solo <850 m. **Marisma −20.2%** (160.4k→128.0k; e<0.20, supply>0.68, rel<1.2). Todo medido con el dump (histograma de orillas y contador de río seco nuevos). 29/29 tests. **Sigue pendiente del ojo de Rodrigo**; siguiente trabajo acordado: **shadow map cercano** para que los modelos proyecten sombra (decisión Rodrigo 2026-07-13, radio ~250 m, sol fijo).
>
> **SESIÓN 2026-07-13 (2) — N2 COMPLETADA con ranks 2+19 (`15e87ed`):** acantilados por **exposición** (fetch de 16 rayos con la rosa de vientos sobre la máscara del prior, calculado ANTES del uplift) × **litología** (dureza de la formación local) — fuera el fbm `cliff_raw` y el proxy `headland`; campo nuevo `TerrainData::coast_cliff` (grid macro). Aplanado de costa y anchura de orilla anclados a él (fin del banding por `climate`, recalibrado 0.25+0.6·(1−cliff) para no saturar). Visual: la costa gana tramos rocosos grises en sectores expuestos y playas en abrigo; silueta más mordida. Métrica: el contador de perchado r=4 queda desenmascarado como estructuralmente inflado por ríos anchos (radio ~12 celdas > ventana 4) — detalles en §7-N2; flotación real (>3 m) sigue 0. **N2 queda a falta únicamente del juicio in-game de Rodrigo.**
>
> **SESIÓN 2026-07-13 — N2 COSTA EMERGENTE, núcleo implementado (commiteado, build+26/26 verdes):** 3 commits por sub-paso (plan de rollback §8 respetado): ① `97213ed` LEM en **modo batimetría** (fondo marino con signo desde prior de plataforma; sin incisión fluvial bajo el agua; ríos gradan al NIVEL DEL MAR; lluvia orográfica se recarga sobre el mar vivo; test nuevo `Lem.FondoMarinoRealEIsloteEmergente`) · ② `0d883ea` **costa emergente**: continentalidad → prior débil de uplift (muere la caída analítica `h -= drop·sea_depth` y el anillo satélite), **transgresión** `coast_flood_m` ~6 m (mecanismo real de las rías, slider "transgresion marina"), fade de borde por **Chebyshev** (rank 22), puerta de **estuario** en hydrology (la cinta del río no pinta lámina sobre terreno ahogado) y desglose del contador de agua perchada en el dump · ③ `197e850` **skerries emergentes**: cadenas de uplift submarino alineadas con el eje tectónico (warpeadas para no delatar la rejilla), la mayoría se queda en bajío, algunas asoman. Verificación: dump (archipiélago NO con estrecho, cadena SE, bajíos legibles bajo el agua) + captura 3D de costa con islote. **Residuo**: agua perchada 12.3k→19.6k celdas (baseline REAL de esta seed medido con stash — el 3.2k del plan era de otra seed); domina el falso positivo de laderas (>2 m: 17-18k, peor caso igual), la parte nueva es llanura costera a cota de mar. **Falta el juicio visual in-game de Rodrigo** (silueta macro, rías, skerries, llanuras costeras); el tuning de cuánta tierra/lóbulos/transgresión es suyo. Quedan del dossier A: acantilados por exposición (rank 2) y banding climático (rank 19).
>
> **SESIÓN 2026-07-09 — AUDITORÍA DE NATURALIDAD (workflow ultracode) + barrido de quick-wins (commiteado, build+24/24 verdes):** Auditoría multi-agente (48 agentes: 7 revisores por dimensión → verificación adversarial de cada finding contra el código → síntesis). 40 findings, **31 supervivientes**. Veredicto: el interior (LEM) es sólido; lo que "canta" se concentra en **costa/islotes estampados** (N2 pendiente), **depósito hidrológico pintado por ruido/hash** (deltas, barras, ibones en rosario, sinuosidad), **agua sin física visual** (sin profundidad/flujo/glitter/espuma) y **luz plana** (sin sombras arrojadas ni AO). Hallazgo relevante: el **"perchado de río" documentado quedó REFUTADO** por el verificador (4 mecanismos lo impiden: incisión solo talla hacia abajo, D8 al vecino más bajo, `enforce_monotone_beds` ×2, sobreexcavación glaciar puerteada a caudal) → reclasificado a mejora menor, NO es el bug que se temía.
> **Barrido elegido por Rodrigo (mejor ratio impacto/esfuerzo), commiteado (2747307…5a9893d):** (#15) **nieve de umbría — BUG de aspecto invertido** (`+n.z` premiaba la solana con el sol default az 135°; ahora el bonus va a la cara opuesta al sol, `generator.cpp`) · (#17) orilla de arena de lago sin corte-isohipsa duro en 60 m (jitter 26–74 m) · (#12) borde de bosque **graduado por `forest_density`** (fin del muro binario `density>0.5`, `veg_renderer.cpp`) · (#21) matas/arbustos sin confeti subpixel (cutoff 900→400 m) · (#13) césped que **empardece con el suelo** (clima+altitud, `grass_field.cpp`) · (#16) estratos de roca sin coherencia global (fase/frecuencia por zona, `chunked_terrain.cpp`) · (#7) comentario de sinuosidad reconciliado con el código. Verificación: dump de sanidad N0 sin cambios (agua flotante 0, litoral 0, 4 regímenes de orilla vivos); residuos de relieve/hidrología **pre-existentes e intactos** (perchado 3248 celdas, picos de relieve base 133/16,8 m — el barrido no los tocaba). **Falta el juicio visual in-game de Rodrigo** (nieve en umbría, césped pardo, feathering de bosque, estratos).
> **Pendiente de la auditoría (NO en el barrido; direcciones para más adelante, Rodrigo decide orden):** **N2 costa emergente** (#1 silueta disco radial → corte LEM×mar, #2 acantilados por oleaje, #20 islotes emergentes, #19 banding climático costero) · **Agua+Luz C4** (#8 sombras CSM+AO, #9 normal analítica de ola/glitter, #10 profundidad Beer-Lambert, #11 flujo D8 en ríos) · **depósito no-fakear** (#3 delta, #4 barras, #5 ibones en rosario, #6 estratos `sharpen_crags` por floor de cota) · no-naturalidades medias sueltas (#18 roca/hierba alpino binario = las "rayas de hierba en vaguada", #14 árboles clonados por tipo mismo VBO). **Ranking completo (31 findings con file:line) + dossiers de implementación por dirección en `docs/auditoria-naturalidad-2026-07-09.md`.**
>
> **SESIÓN 2026-07-08 (tarde) — feedback in-game, commiteado (24/24 tests cada uno):** troncos solo en llano (no flotan) · menos agua en riachuelos (lámina base 0.35→0.15, umbral caudal 1200→2000) · río nace más abajo · césped fuera del backshore de playa · **color menos blocky** (domain warp del borde de material, cerca y media distancia + más variación tonal) · rápidos más confinados (margen 1.15→1.05) · **LUCES** (rebalance sol/ambiente + tonemapping ACES → contraste y volumen, fin del look plano) · **ríos grandes** (cauce principal 20→30 m) · **clamp de picos de un vértice** del relieve (>6 m sobre vecinas). **Queda de la lista de Rodrigo:** glaciares (N4, sim de circos — subjetivo), acantilados naturales (ligado a N2 costa emergente), marisma (afinar look). Sombras proyectadas CSM = siguiente paso mayor de luces.

> **LOOP AUTÓNOMO 2026-07-08 (Rodrigo fuera ~1h) — registro para tu review:**
> **Hecho y commiteado (build+24/24 tests verdes cada uno):** N0 bugs (agua en torres, verde en playa, dithering) ✅ · N1/HP-6 suelo sin cuadraditos + agua sin picos ✅ · césped **grass cards con briznas procedurales (alpha)** — método óptimo, viento, altura por zona, fade sin pop ✅ · peñascos sueltos eliminados ✅ · más árboles + treeline +90 m ✅ · marisma reducida a estuarios muy abrigados ✅ · **río elevado**: agua flotante sobre el lecho 101→0 celdas (cap 3.5→2.5 m, lagos fuera del suavizado) ✅ · **B tamaños de árbol variados** (edad cuadrática 0.5–2.0×) ✅.
> **Residuos documentados (no bloqueantes, para decidir contigo):** (a) *perchado de río* — 3172 celdas de río sin orilla en r=4, peor 13,4 m; la mayoría son falsos positivos (ríos en ladera empinada), pero algún lecho queda sobre un alto local → issue de riverscape de fondo, no urgente. (b) *color en cuadrado* — los bordes de material (hierba↔roca) siguen la rejilla; suavizarlos es render-only pero **subjetivo** (mezclar verde+gris puede embarrar) → tu criterio. (c) *nieve* (N3) — el bonus de cara (`n.z`) puede estar invertido respecto al sol; corregirlo es objetivo pero depende de la convención de sol → confirmar contigo.
> **NO hecho a propósito (te toca juzgar el look):** **N2 costa emergente** y **N4 lagos glaciares** son refactors de generación grandes y de resultado subjetivo (regeneran/retocan el mundo). Hacerlos a ciegas arriesga un mundo feo que te encuentres al volver. Plan de implementación listo en B.1b/B.4 y `investigacion-naturalidad.md`; los ejecutamos con tu visto bueno visual.

> **Actualización 2026-07-08 (2) — NATURALIDAD:** decisiones razonadas de Rodrigo tras `docs/investigacion-naturalidad.md`: (1) **A2 oxbows DESCARTADO por diseño** — en esta isla los ríos fluyen por relieve montañoso, no por llanuras aluviales blandas tipo Amazonas donde se forman los meandros; Agua v2 queda **cerrada en 8/9**. (2) Entra el track de naturalidad: **N1** picos de un vértice (diagnóstico+fix), **N2** costa emergente del LEM (opción B del informe: la silueta y los islotes emergen del contorno h=0 del relieve, fuera el disco radial y el anillo satélite estampado), y **C4 ampliado** con luz (sombras CSM + AO + tonemapping + cielo) y agua (profundidad real + espuma por `wave_energy` + flujo D8 + oleaje direccional). Tras el feedback in-game del mismo día se añaden **N0** (bugs: agua en torres, verde en playa, dithering de orilla) y **N4 lagos glaciares** (circos simulados, HP-5 cerrada). **Orden confirmado por Rodrigo: N0 → N1 → N2 → N4 glaciares → N3 nieve → B árboles → C (C4 ampliado) → E.**

> **Actualización 2026-07-08 — REFOCUS (sustituye a la nota TFG del 2026-07-07):** este proyecto pasa a ser un **proyectín personal**: un mundo generado proceduralmente **realista y lo más natural posible**, sin framing de portfolio Aechelon/Shield AI. Decisiones de Rodrigo (2026-07-08): (1) el track de amplificación de DEMs queda **DESCARTADO como TFG** — el TFG saldrá de un problema que Rodrigo encuentre e investigue él mismo, en un proyecto futuro aparte; la carpeta `tfg/` y sus resultados (E0, T1b) se conservan como experimento archivado (Apéndice C, tombstone). (2) El alcance final es la **isla v1.0**: Fases A2 → B → C → E y cierre; la Fase D (planeta cube-sphere) queda **descartada** (tombstone en B.4). (3) Rige el **Protocolo Socrático** (`~/.claude/CLAUDE.md`): la IA escribe el código; las decisiones no triviales de arquitectura/resolución las toma Rodrigo.

---

## 0. CÓMO RETOMAR ESTE PROYECTO SIN CONTEXTO (léeme primero)

Si estás leyendo esto sin acceso a las conversaciones previas (otro modelo, otra sesión, otro humano), este es el orden de arranque:

1. **Lee este plan entero.** Es el artefacto canónico: estado, fases, criterios y manual de operaciones. Ante conflicto entre este plan y el README, **gana este plan**.
2. **Documentos de soporte, en este orden:**
   - `docs/superpowers/specs/2026-07-02-procedural-terrain-design.md` — el diseño aprobado del juego/motor.
   - `docs/investigacion-agua-v2.md` — algoritmos y fuentes de los 9 pasos de agua (8 ✅; 9 oxbows ❌ descartado por diseño 2026-07-08).
   - `docs/investigacion-naturalidad.md` — diagnóstico y opciones elegidas del track de naturalidad (N1 picos, N2 costa emergente, C4 luz+agua).
   - `docs/auditoria-naturalidad-2026-07-09.md` — auditoría multi-agente (31 findings verificados) con ranking file:line y dossiers de implementación por dirección (N2 costa, Agua+Luz C4, depósito no-fakear). Punto de partida ejecutable para el siguiente frente.
   - `docs/investigacion-lem.md` — trasfondo del LEM si tocas `terrain/lem`.
   - (📦 archivado 2026-07-08, ya no es parte del proyecto) `docs/investigacion-amplificacion-dem.md` y `tfg/` — el antiguo track TFG; solo consultar si algún día se retoma como proyecto aparte.
3. **Estado a 2026-07-08:** Fase A **CERRADA en 8/9** (A1 ✅; A2 oxbows descartado por diseño). Siguiente: **N1 picos de un vértice** → N2 costa emergente → B → C (C4 ampliado con luz+agua) → E y cierre en `v1.0-isla`. El track TFG está descartado (ver notas de cabecera).
   **Estado a 2026-07-10:** luz precomputada ✅ (parte de C4-L: sombra arrojada ray-marched + AO de cielo horneados por vértice sobre grid ≤1024, sol fijo 135°/40° — si se mueve el sol del panel, la sombra baked NO le sigue; aplicada a terreno, césped y modelos). **Glaciación (N4) ELIMINADA**: daba lagos-pozo >100 m y su tope estaba acoplado al drenaje (agua perchada ×6); los lagos de altura ahora emergen solo de relieve+priority-flood — **N4 DESCARTADO definitivo (decisión de Rodrigo confirmada 2026-07-11)**: la isla v1.0 no lleva ibones de circo. Seed por defecto 20260711 (mundo regenerado). **Ríos naciendo en cresta RESUELTO** (breach de depresiones someras: el sillín fino se rompe y drena en vez de embalsarse; el charco de cota 930 en (1300, 492) confirmado breached, 0 lagos de montaña restantes; test `UnCharcoSomeroEnAltoNoSeEmbalsa`). **Disección uniforme del LEM ("U de cabeceras", feedback in-game 2026-07-10):** diagnóstico con el hillshade del dump (`--dump-costa` ahora escribe `-relieve.bmp`) — UNA longitud de onda de barrancos en toda la isla por constantes globales del LEM. Rodrigo eligió las 4 vías físicas; **hechas 2 de 4** (secuencial, un commit por mecanismo): ① **litología por formaciones** (bandas cuantizadas en coords tectónicas modulan K ±1.7× y el área crítica A₀ ±2.5× — A₀ es quien fija la anchura de barranco) y ② **precipitación orográfica** (viento por seed + advección de humedad con recarga marina y sombra de lluvia, caudal del LEM = área×lluvia, normalizada a media 1 con suelo 0.5 — suelo 0.3 pasaba el contrato de cota 1.30×: cumbre 1862 vs 1820). Pendientes ③ uplift transitorio (pulsos/knickpoints — el más invasivo, esperar visto bueno visual de ① + ②) y ④ (absorbida en ①: la resistencia de roca ya modula los umbrales). Verificación: grano por sectores + asimetría barlovento/sotavento visibles en el hillshade; 25/25 tests.
   **Estado a 2026-07-13:** N2 costa emergente — núcleo ✅ (LEM batimetría + prior débil + transgresión + skerries, ver nota de sesión en cabecera). Pendiente: juicio in-game de Rodrigo (paleta C4, litología/orográfica ①②, barrido quick-wins Y ahora N2), acantilados por exposición (rank 2), banding climático (rank 19). Después: N3 sotavento (opcional) → B vegetación → C → E.
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
- Texturas realistas / PBR / normal maps — el flat shading por material es decisión estética cerrada. ⚠ 2026-07-08: Rodrigo ha puesto el facetado en duda (HP-6, resolución escalonada) — sigue cerrado hasta la re-evaluación de la fase 1 de HP-6.
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
| N0 | Bugs de feedback (2026-07-08) | ✅ **CERRADA 2026-07-08** (commits `1e42e93`…`c8ee9f3`): agua flotante 45.652→0 celdas sobre invariante (3 causas: regla de mínimo en pisos de cascada, tope de rampa, invariante de lámina ≤3.5 m) · verde a pie de agua 0 celdas (ángulo de reposo en la puerta de orilla) · dithering 4.149→143 (filtro de moda 2 pasadas). Sanidad instrumentada en `--dump-costa` | — | — |
| N1/HP-6 | Cuadraditos del suelo + agua rota | ✅ **CERRADA 2026-07-08** (commit `2b757d9`, verificado en captura): jitter de vértices + diagonal alternada + tinte por value-noise + smooth-blend cercano → suelo sin rejilla; suavizado de lámina de río (3 pasadas) + invariante acotado → agua sin picos. Seed reiniciada 31416→20260709. **Pendiente picos de un vértice del RELIEVE** (detail_amplitude), que es distinto de la retícula ya resuelta |
| N1b | Picos de relieve + rayas de hierba en ladera | 🟡 **Parcial**: (a) clamp de picos de un vértice >6 m hecho (`8ad0d70`); contador 73→60 tras precipitación orográfica (`47d7c2e`) — residuo no bloqueante · (b) rayas de hierba = finding #18 de la auditoría (roca/hierba alpino binario), pendiente de decisión de Rodrigo (¿natural o artefacto?) | BAJO | residuo |
| N2 | Costa emergente del LEM | ✅ **IMPLEMENTADA COMPLETA 2026-07-13** (`97213ed`…`15e87ed`): LEM en modo batimetría, prior débil, transgresión (rías), skerries emergentes, fade Chebyshev, acantilados por exposición×litología (rank 2) y fin del banding climático de costa (rank 19). 26/26 tests, dumps y capturas verificados — **pendiente SOLO el juicio in-game de Rodrigo** (silueta/tuning: lóbulos, radio, transgresión) | BAJO (tuning) | 0–1 |
| ~~N4~~ | ~~Lagos glaciares (circos)~~ | ❌ **DESCARTADO 2026-07-10 (confirmado por Rodrigo 2026-07-11)**: se implementó (`7e8ddce`, `786f1aa`) y se **eliminó** en `6c17d2f` — daba lagos-pozo >100 m y agua perchada acoplada al drenaje (×6). Los lagos de altura emergen solo de relieve + priority-flood. La isla v1.0 no lleva ibones | — | — |
| N3 | Nieve realista | 🟡 **Parcial**: inversión de aspecto CORREGIDA (`2747307`, la umbría ya retiene nieve — pendiente juicio in-game). Queda: acumulación a sotavento con la rosa de vientos (opcional, decidir si aporta) | BAJO | 0–1 |
| B | Vegetación — **AMPLIADA** (feedback 2026-07-08) | 🟡 **Auditoría B.2 HECHA 2026-07-13** (`d867231`): censo por tipo en ImGui+log, marisma con matas (estaba estéril), densidades por bioma revisadas (rocas=0 por decisión). Antes (2026-07-08): césped v2 con viento/altura por zona/fade, árboles ×2, treeline +90 m, tamaños por edad cuadrática. **Pendiente: verificación in-game de Rodrigo** (censo, marisma, viento/densidad del césped, repetición rank 14) | BAJO | 0–1 |
| C | Hito 6 — pulido | C1 presets JSON ✅ **2026-07-13** (`7cc69e5`, 3 tests, verificación de hash en runtime) · C2 minimapas ✅ **2026-07-13** (`23c60f9`, verificado con captura) · C3 rendimiento 🟢 **medido 2026-07-13**: 114-126 fps Release en las vistas más caras y generación 18 s — nada que optimizar; confirmar en vuelo libre · **C4 AMPLIADO**: luz ✅ (L1 CSM descartado — sol fijo; L2 AO ✅; L3 ACES ✅; L4 cielo ✅) + agua 🟡 **implementada completa 2026-07-13 SIN verificar en pantalla** (W1 calado Beer-Lambert, W2 espuma por `wave_energy`, W3 flujo D8, W4 mar direccional — detalle en B.1b; review visual de Rodrigo pendiente) + **paleta** 🟡: 1ª pasada `0ba3edb`; review xhigh 2026-07-11 → 11 hallazgos (raíz: cotas hardcodeadas desacopladas de snow_line/mountain_height/sea_level + formaciones de ruido no-seeded en vez de litología LEM); **fix profundo (opción A) HECHO 2026-07-11**: `palette.{h,cpp}` compartida render+dump, anclada a litología LEM / snow line efectiva / cumbre real / sea_level / lámina local; verificado con dump (25/25 tests, hash intacto) — **pendiente juicio in-game de Rodrigo** | MEDIO (C3) | 4–8 |
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

**Fase N0 (✅ cerrada 2026-07-08):**
- [x] Contador offline en `--dump-costa`: **0 celdas** de río con lámina por encima del invariante (≤3.5 m sobre lecho; antes 45.652 celdas, peor caso 50.7 m). Pendiente de confirmación in-game por Rodrigo en (455, −1335) y (−2941, 231).
- [x] Dump de materiales: **0 celdas** de hierba/bosque a pie de agua (rel<3.9 m, sin filtro de pendiente). El test `SinHierbaAPieDeAgua` ya no copia el agujero del clasificador.
- [x] Dithering de orilla 4.149 → **143 celdas** (−96.6%); recorte visual verificado: bandas coherentes, transiciones orgánicas.

**Fase N1:**
- [ ] Culpable identificado con evidencia (H1/H2/H3 del informe §3, capturas antes/después).
- [ ] Contador de picos (celda > máx. de sus 8 vecinos + umbral talus) ≈ 0 tras el fix, sin planchar crestas legítimas (captura de la misma cumbre antes/después).
- [ ] `ctest` verde (hash regenerado solo si el fix toca generación, con justificación en el commit).

**Fase N2 (núcleo implementado 2026-07-13, commits `97213ed`/`0d883ea`/`197e850` — pendiente juicio in-game de Rodrigo):**
- [x] La silueta y los islotes salen del contorno h=0 del relieve LEM: sin `max()` de anillo satélite ni smoothstep radial como forma dominante (solo prior débil + condición de borde). **Hecho**: LEM en modo batimetría (fondo marino con signo, sin incisión fluvial bajo el agua, ríos gradando al nivel del mar) + transgresión `coast_flood_m` (~6 m, mecanismo real de las rías).
- [x] Captura aérea: bahías/rías donde hay valles, cabos donde hay divisorias, ≥2 islotes/skerries emergentes de tamaño no uniforme. **Verificado en dump 2026-07-13** (archipiélago NO con estrecho, cadena SE alargada, islote NE; bajíos sumergidos visibles) + captura 3D de la costa. Falta el veredicto visual de Rodrigo.
- [x] El mar sigue rodeando el dominio (invariante isla, ahora con fade Chebyshev al borde) y los 4 regímenes de orilla de A1 sobreviven (`EsUnaIslaConMarAlrededor` y `VariedadDeOrillas` verdes).
- [x] `ctest` verde (26/26; test nuevo `Lem.FondoMarinoRealEIsloteEmergente`). Nota: no había hash golden que regenerar — el test de seed es de determinismo puro.
- [x] **Rank 2 HECHO 2026-07-13 (`15e87ed`)**: acantilado = **fetch real** sobre la máscara del prior (16 rayos, misma rosa de vientos que el oleaje, ANTES del uplift — rompe la circularidad costa↔oleaje) × **resistencia litológica** (la misma formación que consume el LEM). Fuera el `cliff_raw` fbm y el proxy `headland`. El campo viaja en `TerrainData::coast_cliff` (grid macro).
- [x] **Rank 19 HECHO 2026-07-13 (mismo commit)**: el aplanado de costa y la anchura base de orilla se anclan en `coast_cliff` local (frente acantilado → orla estrecha de roca; costa abrigada/blanda → playa ancha), no en el sector global de `climate`. Rango del aplanado recalibrado (0.25+0.6·(1−cliff)): a 0.9 saturaba y duplicaba el perchado costero.
- El tuning macro de silueta (fuerza de lóbulos, `island_radius`, `coast_flood_m` como slider "transgresion marina") lo decide Rodrigo en vivo.
- **Hallazgo de métrica (2026-07-13)**: el contador "agua perchada r=4" está estructuralmente inflado por los ríos ANCHOS — con `river_max_width_m=30` el cauce tiene radio ~12 celdas y la ventana r=4 cae entera dentro de la lámina (los bancos quedan fuera de la ventana en cuanto hay pendiente), así que cuenta el interior del río como "sin orillas". Baseline real de esta seed 12.3k; tras N2 completo ~31k (más río ancho en llanura costera). El contador de flotación REAL (lámina >3 m sobre lecho) sigue en **0**. Si se quiere una métrica útil, subir la ventana a r>radio_max o excluir el interior del canal — decidir con Rodrigo.

**~~Fase N4 (lagos glaciares)~~ — DESCARTADA 2026-07-10 (confirmado 2026-07-11):**
- Se implementó y verificó (`7e8ddce`: ibones 844–871 m, hasta 236 celdas; `786f1aa`: valle en U), pero producía lagos-pozo >100 m y agua perchada acoplada al drenaje (×6) → **eliminada en `6c17d2f`**. Los lagos de altura emergen solo de relieve + priority-flood. La isla v1.0 no lleva ibones de circo.

**Fase B (auditoría hecha 2026-07-13, `d867231` — pendiente verificación in-game):**
- [x] Contador de instancias por tipo: panel ImGui "Vegetacion" (pinos/frondosos/rocas/matas/arbustos/troncos + total) y línea `vegetacion: ...` en el log al cocer el mundo. **Criterio enmendado**: son 5/6 modelos en uso — las rocas están a 0 **por decisión de Rodrigo** (feedback 2026-07-08 "quitar los peñascos"), el contador lo hace visible, no se reintroducen.
- [x] Densidad respeta bioma: sin árboles en arena/roca/nieve/agua ✓ (switch solo Forest/Grass/Sand/Marsh); matas en pradera ✓; troncos solo en bosque y suelo llano ✓; borde de bosque graduado por `forest_density` ✓ (2026-07-09). **Nuevo**: la marisma ya no está estéril — matas de junco densas (tuft 0.12) y arbusto ocasional, sin árboles (suelo anegado).
- [ ] Verificación in-game de Rodrigo: censo en el panel (troncos > 0, matas de marisma visibles) + repetición de siluetas de árbol (rank 14, mismo VBO por tipo — solo si "canta" en vivo). Capturas de esta sesión abortadas: el PC estaba en uso.

**Fase C:**
- [x] **C1 HECHO 2026-07-13 (`7cc69e5`)**: Guardar/Cargar presets `presets/*.json` (nlohmann-json vía vcpkg; `terrain/preset.{h,cpp}` serializa el `TerrainConfig` COMPLETO campo a campo, con `height_hash` FNV-1a del heightfield al guardar). Cargar → regenerar verifica el hash en runtime y lo reporta en panel+log. Protegido por 3 tests (`Preset.RoundTripDeConfigCompleta`, `Preset.CargarYRegenerarReproduceElMundo` — el criterio §7-C exacto —, `Preset.CargarFicheroCorruptoNoRevienta`). 4 presets de demo commiteados (isla-canonica, archipielago-bajo, macizo-alpino, isla-verde-suave) — **su carácter visual lo valida Rodrigo** (los 3 nuevos son parámetros razonados sin verificar en pantalla; el hash se estampa al guardarlos desde la app).
- [x] **C2 HECHO 2026-07-13 (`23c60f9`)**: pestaña "Campos" en ImGui con 5 minimapas (`ui/field_maps.{h,cpp}`, texturas 512² reconstruidas al montar mundo, coste cero cerrada): altura (hipsométrico+batimetría), caudal (log), clima, biomas (color por material) y oleaje. Verificado con captura (altura/caudal/clima renderizando; el caudal sale pálido a 256 px — si molesta, subir contraste de la rampa).
- [x] **C3 MEDIDO 2026-07-13** (instrumentación ya existía: fps/ms + "ultima generacion" en el panel): Release, muestras en las vistas más caras — panorámica 2500 m con 1414/4096 chunks: **114.6 fps**; ras de bosque denso con 2673/4096 chunks: **126.0 fps**. Según B.3-C3.2, con ≥60 no se optimiza nada. *Pendiente solo la confirmación de Rodrigo en vuelo libre a 100 m/s* (las muestras son poses estáticas).
- [x] **Generación 4096² Release: 18.0 s** (cronometrada con el dump completo incluido) ≤ 30 s ✓.

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
| ~~HP-5~~ | ~~Lagos de montaña: ¿cómo?~~ | **CERRADA 2026-07-08**: erosión glaciar simulada (Fase N4) — es el proceso real de los ibones y encaja con A.5 | — |
| HP-6 | Suelo "geométrico" — aclarado 2026-07-08: "veo cuadraditos, no es smooth al 100% como la vida real" + el facetado low-poly le convence menos | **Resolución ESCALONADA**: fase 1 (dentro de N1/C4) romper la retícula sin abandonar el estilo — jitter determinista de vértices XZ (mata los cuadraditos), blended-smooth más agresivo con la distancia, densidad de malla lejana (LOD); fase 2: re-evaluar con capturas — si sigue sin convencer, decisión de pilar estético (smooth shading, reabre §2) que toma Rodrigo explícitamente | Rodrigo |

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
- **Al añadir/quitar campos de un struct compartido entre TUs (p. ej. `TerrainData` en `generator.h`), rebuild LIMPIO** — `cmake --build cmake-build-debug-opengl --clean-first`. El build incremental puede dejar `.obj` stale con el layout viejo (`terrain_tests.obj` vs `generator.obj`) → `sizeof` distinto → **RTC #2 "Stack around variable corrupted"** en cada `generate()` (ventana modal de Debug que cuelga los tests horas). Precedente: el desbordamiento de `counts[8]` de 2026-07-07. Diagnosticado y resuelto 2026-07-09 tras añadir `shadow`/`sky_ao`/`light_res`.

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

### N0 — Bugs del feedback in-game (2026-07-08)

1. **Agua en torres**: `build_water_mesh` (mesher.cpp:68) usa `water_level` propio sin clamp contra el terreno actual — instrumentar contador `wl − h > 2 m`, localizar (¿knickpoints? ¿celdas con terreno mutado post-hidrología?) y arreglar la causa (clamp físico o recomputar `wl` tras la última mutación del heightfield), no el síntoma.
2. **Verde en playa**: dentro de la banda litoral, `slope ≥ 0.35` cae al clasificador general → Grass (generator.cpp:465). Fix candidato: dentro de la banda, los no-orilla caen a arena/roca según pendiente (una duna empinada es arena, no prado) — verificar con dump.
3. **Dithering grava/roca en arena**: umbrales duros (e≈0.45, supply≈0.30) sin histéresis — suavizar la frontera del régimen a lo largo del contorno (el blur de anchura ya existe; falta para el TIPO).

### ~~N4 — Lagos glaciares / circos~~ (TOMBSTONE — descartado 2026-07-10, confirmado 2026-07-11)

Se implementó tal como estaba planeado (máscara por ELA → sobreexcavación de cabeceras → priority-flood llena las cubetas; `7e8ddce` + refinado `786f1aa`) y **funcionó** (ibones de circo reales a 844–871 m). Se descartó igualmente en `6c17d2f`: la sobreexcavación producía lagos-pozo >100 m de profundidad y su tope estaba acoplado al drenaje (agua perchada ×6). Decisión de Rodrigo: la isla v1.0 va **sin glaciación**; los lagos de altura emergen solo de relieve + priority-flood. El detalle técnico queda en el historial de git de erosion.cpp/hydrology.cpp por si un futuro mundo lo justificara.

### C4 ampliado — luz y agua (se ejecuta dentro de la Fase C)

- Luz: L1 sombras CSM (2-3 cascadas) — ❌ **DESCARTADO 2026-07-11 por decisión de Rodrigo**: sol fijo, la sombra arrojada precomputada se mantiene (día/noche sigue siendo stretch post-v1.0, HP-1) · L2 AO por vértice precomputado ✅ (2026-07-10) · L3 tonemapping+gamma ✅ ACES (2026-07-08) · L4 ✅ **HECHO 2026-07-11**: pase de cielo fullscreen (gradiente cenit→horizonte + disco solar + halo Mie), niebla exponencial CON ALTURA (integral analítica, H=500 m + término residual de perspectiva aérea), `sky_color(dir)` compartido por GLSL para que la niebla de todos los materiales funda exacto al cielo, océano infinito analítico bajo el horizonte (la malla del mar y su far-clip ya no se ven: olas aplanadas a >4 km y alpha→0.97 a >6 km), fresnel del agua reflejando el cielo real. Verificado en 3 vistas (hacia el sol, altura anti-sol, pie de playa).
- Agua: ✅ **IMPLEMENTADA COMPLETA 2026-07-13 (`6a855be`/`90ffc3e`/`68559ff`) — SIN VERIFICAR EN PANTALLA (petición de Rodrigo: "completa y luego reviso, no ejecutes")**. W1 color+alpha por calado real (Beer-Lambert por canal; ríos con calado por vértice en `a_normal.y`, mar por píxel desde textura RG16F 2048² calado+oleaje que main hornea al montar mundo) · W3 ríos que fluyen (dirección de corriente por vértice — descenso del lecho, lagos quietos —, onda de vértice y rizado analítico de normal advectados aguas abajo, espuma de rápidos/cascadas desde `whitewater` que ya exportaba la hidrología) · W4 mar direccional (normal analítica por píxel de 3 trenes de ola alrededor de la dirección de viaje de la rosa de vientos, abanico ∝ 1−focus; olas de vértice reorientadas; glitter/fresnel por píxel) · W2 espuma de rompiente (franja donde el fondo roza superficie × energía de OLEAJE REAL × pulso animado). Regla de oro respetada: todo anclado a campos simulados (calado, D8, whitewater, wave_energy, rosa). **Riesgo declarado**: el GLSL está auditado a mano pero no compilado por el driver — si algo peta al arrancar, el log del Shader lo dirá; los knobs de tuning (k_abs, amplitudes de ola, umbrales de espuma) están comentados en `kWaterFrag`.
- Paleta: ✅ **primera pasada 2026-07-11** (feedback "montañas muy uniformes"): roca con gradiente de altitud (parda-cálida abajo → gris-azulada oscura arriba, contraste real contra la nieve), matiz por clima y campo de formación ~600 m (dos paredes separadas por km ya no comparten gris); nieve sucia junto al límite → blanca en lo alto; lecho marino oscurece con la profundidad. **Pendiente el juicio in-game de Rodrigo.**
- Regla de oro: cada efecto visual se ancla a un campo físico ya simulado — nada decorativo desacoplado del proceso.
- **Plan montañas más naturales (decidido 2026-07-11, Rodrigo)**: paleta (✅ arriba) → **ruido alineado al flujo** (pendiente #1 de `investigacion-lem.md`, Grenier 2024) → **MFD** (pendiente #2, Anand 2020; toca solver + hash). Investigado 2026-07-11: no existe "generador mejor" que sustituya al LEM — el stream power law ES el estado del arte (Cordonnier 2016, Schott 2023, Tzathas 2024); lo que hay son técnicas para injertar.

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
