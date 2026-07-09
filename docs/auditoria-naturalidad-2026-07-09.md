# Auditoría de naturalidad del terreno — 2026-07-09

> **Método:** workflow multi-agente (ultracode). 7 revisores por dimensión (relieve, hidrología, costa, materiales/color, nieve, render luz+agua, vegetación) → **verificación adversarial** de cada finding contra el código real (línea confirmada, mitigación buscada, rasgo natural legítimo descartado) → síntesis con ranking. **48 agentes, 40 findings reportados, 31 supervivientes.**
> **Regla:** este doc SEÑALA problemas y propone direcciones con trade-offs; **las decisiones de qué atacar y cómo las toma Rodrigo** (Protocolo Socrático). La IA escribe el código.
> **Principio rector intacto:** "no fakear nada" (plan §A.5) — cada rasgo debe emerger de la simulación; el ruido solo modula procesos.

---

## 0. Veredicto global

El **interior es sólido**: el LEM esculpe relieve creíble (crestas, valles, divisorias). Lo que delata el mundo como artificial se concentra en cuatro familias:

1. **Costa e islotes ESTAMPADOS** por un campo de distancia radial + ruido (silueta, satélites, acantilados). La línea de agua no emerge del corte relieve×nivel del mar. *(N2 pendiente — el mayor fakeo estructural.)*
2. **Depósito hidrológico "pintado"** por ruido/hash en vez de transporte de sedimento (deltas, barras de canal, ibones en rosario, sinuosidad de meandros).
3. **Agua sin física visual**: sin color por profundidad, sin flujo direccional en ríos, normal plana acristalada sin sol-glitter, sin espuma pese a `wave_energy` calculado.
4. **Luz plana**: sin sombras arrojadas ni AO pese a geometría (acantilados, valles en U) que las proyectaría.

Transversal: **umbrales binarios duros** donde ya existe un gradiente calculado (borde de bosque, roca/hierba, arena de lago) → muros y discontinuidades a cota fija.

### Hallazgo de la verificación adversarial: "perchado de río" REFUTADO
El residuo documentado *perchado de río* (cauce sobre un alto local) quedó **refutado** al leer el código: cuatro mecanismos lo impiden — (1) la incisión solo talla hacia abajo hacia la receptora (`hydrology.cpp` "solo talla, nunca eleva"), (2) el D8 elige siempre el vecino más bajo, (3) `enforce_monotone_beds` corre dos veces eliminando contrapendientes, (4) la sobreexcavación glaciar está puerteada a celdas con caudal. Reclasificado a **mejora menor de bajo impacto**. No es el bug que se temía.

---

## 1. Ranking completo (31 findings verificados)

Severidad: `bug` (defecto objetivo) · `no-fakear` (rasgo estampado que viola A.5) · `naturalidad` (se lee artificial, subjetivo) · `mejora` (oportunidad). **✅ = resuelto en el barrido del 2026-07-09.**

| # | Rasgo | Fichero:línea | Sev | Impacto | Esf | Estado |
|---|---|---|---|---|---|---|
| 1 | Silueta = disco radial disfrazado, no corte LEM×mar | `generator.cpp:56` | no-fakear | medio | alto | pendiente (N2) |
| 2 | Acantilados por sector de ruido (`cliff_raw`), no por oleaje | `generator.cpp:85` | no-fakear | medio | medio | pendiente (N2) |
| 3 | Delta = disco smoothstep + distributarios por hash | `riverscape.cpp:662` | no-fakear | medio | alto | pendiente |
| 4 | Barras fluviales = elipses gaussianas por espaciado+hash | `riverscape.cpp:869` | no-fakear | medio | medio | pendiente |
| 5 | Ibones en rosario: poza/umbral lo decide un fbm | `hydrology.cpp:290` | no-fakear | medio | medio | pendiente |
| 6 | Estratos `sharpen_crags` por `floor(cota)`: tarta de pisos | `generator.cpp:306` | no-fakear | medio | medio | pendiente |
| 7 | Sinuosidad = bump fbm a toda la llanura + comentario desincronizado | `hydrology.cpp:119` | no-fakear | medio | bajo | ✅ comentario reconciliado (estructural documentado, no tocado) |
| 8 | Sin sombras arrojadas ni AO: luz plana | `terrain_shaders.h:93` | mejora | medio | alto | pendiente (C4) |
| 9 | Especular/fresnel sobre normal plana: mar acristalado sin glitter | `terrain_shaders.h:289` | naturalidad | medio | medio | pendiente (C4) |
| 10 | Agua sin color por profundidad (mezcla constante 0.45) | `terrain_shaders.h:298` | naturalidad | medio | medio | pendiente (C4) |
| 11 | Río y mar comparten oleaje: el río no fluye, bota como mar | `terrain_shaders.h:268` | naturalidad | medio | medio | pendiente (C4) |
| 12 | Borde de bosque = muro binario `density>0.5` | `veg_renderer.cpp:84` | naturalidad | medio | bajo | ✅ graduado por forest_density |
| 13 | Césped verde fijo sobre suelo agostado/pardo | `grass_field.cpp:151` | naturalidad | medio | bajo | ✅ empardece con clima+altitud |
| 14 | Árboles de un tipo geométricamente idénticos (mismo VBO) | `models.cpp:121` | naturalidad | medio | medio | pendiente |
| 15 | **Nieve de umbría INVERTIDA** (bonus a la cara soleada) | `generator.cpp:535` | bug | medio | bajo | ✅ aspecto referido al sol real |
| 16 | Estratos de roca = seno global de la cota, mismas bandas en toda la isla | `chunked_terrain.cpp:20` | naturalidad | medio | bajo | ✅ fase/frecuencia por zona |
| 17 | Anillo de arena de lago con corte duro en 60 m | `generator.cpp:518` | naturalidad | medio | bajo | ✅ jitter 26–74 m |
| 18 | Roca/hierba alpino binario por pendiente → **rayas de hierba en vaguada** | `generator.cpp:551` | naturalidad | medio | medio | pendiente |
| 19 | Un solo `climate` de ruido manda acantilado+ancho playa+aplanado (banding) | `generator.cpp:60` | naturalidad | medio | medio | pendiente (N2) |
| 20 | Islotes = anillo satélite en banda radial fija | `generator.cpp:62` | naturalidad | medio | bajo | pendiente (N2) |
| 21 | Matas/arbustos dibujados a ~900 m: confeti subpixel | `veg_renderer.cpp:248` | mejora | medio | bajo | ✅ cutoff 400 m |
| 22 | Recorte circular del perímetro sin warp: contorno de disco | `generator.cpp:73` | naturalidad | bajo | bajo | pendiente (N2) |
| 23 | Corte glaciar con salto vertical en la isohipsa `ela-120` | `erosion.cpp:294` | naturalidad | bajo | bajo | pendiente |
| 24 | Treeline = offset fijo bajo la nieve (isohipsa sin clima/aspecto) | `generator.cpp:402` | naturalidad | bajo | bajo | pendiente |
| 25 | Línea de nieve = cota global, ignora `climate` | `generator.cpp:534` | mejora | bajo | bajo | pendiente |

*(Nota: los ranks 6 findings adicionales dedup/PLAUSIBLE del detalle por dimensión no se listan aparte; ver output del workflow si hace falta el desglose por agente.)*

---

## 2. Dossiers de implementación (para cuando Rodrigo elija dirección)

Cada dossier: **qué, dónde, aproximación física, sub-pasos, riesgo/tests, y los puntos de decisión que siguen siendo de Rodrigo.** Ninguno está iniciado.

### Dirección A — N2 costa emergente (ranks 1, 2, 19, 20, 22)
**El fakeo estructural #1.** Ya hay un informe de opciones en `docs/investigacion-naturalidad.md §1` (Rodrigo se inclinó por la **opción B**). Es el cambio de generación más gordo del track.

- **Núcleo (rank 1):** en `coast_at` (`generator.cpp:44-91`), la continentalidad deja de ser LA forma (`continent = 1 - smoothstep(island_radius, 1, d)`) y pasa a **prior débil de uplift**; el LEM corre con el dominio extendido **bajo el nivel del mar** (fondo marino real, no la caída analítica `h -= drop·sea_depth` de `build_base_relief:185-220`); la costa = **contorno h=0** del relieve erosionado.
- **Islotes (rank 20):** retirar el bloque `satellite` (`generator.cpp:62-68`); los islotes deben emerger como picos del prior que apenas asoman tras LEM+inundación.
- **Acantilados (rank 2):** la decisión acantilado/playa debe venir de **exposición+resistencia**, no del `cliff_raw` fbm. Requiere una pasada de fetch barata sobre la máscara de continentalidad ANTES del perfil de costa (romper el orden costa↔oleaje actual).
- **Banding climático (rank 19):** que ancho de playa y aplanado dependan de variables físicas locales (`wave_energy`, `river_supply`, pendiente), no de un sector global de `climate`.
- **Perímetro (rank 22):** el fade a mar del borde usa distancia radial cruda (arco de círculo). Aplicarlo sobre distancia Chebyshev/al borde del cuadrado o sobre la coordenada warpeada.
- **Riesgo/tests:** **regenera todos los mundos** — el hash de `Generator.DeterministaPorSeed` no cambia (sigue determinista) pero los tests de invariante de isla (`EsUnaIslaConMarAlrededor`, `VariedadDeOrillas`) deben re-validarse; conservar el invariante "el mar rodea el dominio" como condición de borde débil. Commits pequeños por sub-paso (prior → fondo marino → islotes → acantilados), captura en cada uno. Rollback: revert si tras 2 intentos de tuning no convence.
- **Decide Rodrigo:** cuánta tierra sale (`island_radius` pasa a escalar el prior, no el radio directo); fuerza del prior; si se hace la opción B o se escala a C (retroceso costero simulado, "Agua v3").

### Dirección B — Agua + luz (C4 render) (ranks 8, 9, 10, 11)
**Máximo salto de realismo visual, casi todo shader.** Cada efecto anclado a un campo ya simulado (regla de oro C4).

- **W1 profundidad real (rank 10):** hornear `depth = water_level - h(fondo)` por vértice en la malla de agua (`mesher.cpp build_water_mesh`) → interpolar entre tono somero claro y `deep` con atenuación Beer-Lambert en `kWaterFrag`. Campos: heightfield + water_level. **Sin ruido.**
- **W3 flujo en ríos (rank 11):** separar el término fluvial del marino. Hornear por vértice la **dirección de flujo D8** (ya en `hydrology`) y advectar el patrón de ondas/normal en ese sentido para ríos; el oleaje de fetch queda para el mar (`build_sea_plane`). Hoy ambos comparten `kWaterVert/kWaterFrag` con 2 senoidales diagonales fijas.
- **W4/glitter (rank 9):** calcular una **normal analítica de ola** en el fragment (suma de ondas / Gerstner low-poly, independiente de la teselación) para distribuir sol-glitter y fresnel por píxel; orientar con la rosa de vientos (`wind_dir_deg/focus`). Hoy la normal geométrica de la rejilla gruesa (~625 m/celda) deja el especular encendiéndose en bloque.
- **W2 espuma (bajo):** franja de espuma ∝ `wave_energy` en la costa (el diferencial "no fake" — nadie más tiene ese campo).
- **L1/L2 luz (rank 8):** **sombras arrojadas** — precomputar del heightfield un shadow/horizon map en la dirección del sol (determinista, offline) + **sky-visibility/AO por vértice** (horizon-based, 8-16 direcciones). Inyectarlos como atenuación del diffuse y del ambiente en `kTerrainFrag`. L1 es lo único que toca la arquitectura de render (pase de profundidad o campo precomputado); AO encaja en la generación como campo tipo `wave_energy`.
- **Riesgo/tests:** shader-only (W1/W3/W4/W2) no toca tests. AO precomputado añade un campo a `TerrainData` (sin romper hash de heightfield). L1 sombras es el más caro. **Decide Rodrigo:** sombras CSM en tiempo real vs shadow map precomputado del sol fijo (encaja con el pipeline offline y el sol default).

### Dirección C — Depósito no-fakear (ranks 3, 4, 5, 6)
**Pureza filosófica.** Despintar el ruido/hash y hacer emerger de proceso. Resultado subjetivo, varios ya muy amortiguados → menor ratio impacto/esfuerzo.

- **Delta (rank 3, `riverscape.cpp:662`):** hoy disco de deposición smoothstep + distributarios por `hash01`. Progradar desde deposición real de sedimento (masa transportada vs capacidad) en las bocas. *Alternativa barata: aceptarlo como cosmético y documentarlo explícitamente como tal.*
- **Barras de canal (rank 4, `riverscape.cpp:869` `stamp_channel_bars`):** elipses gaussianas por espaciado+hash. Emerger de un balance sedimentario local (deposición en el interior de la curva / donde baja la velocidad). El nombre `stamp_` delata el estampado.
- **Ibones en rosario (rank 5, `hydrology.cpp:290`):** la alternancia poza/umbral y la profundidad salen de un fbm; los criterios físicos solo son puertas. Derivar la localización de cubetas de un proceso (sobreexcavación ∝ flujo de hielo acumulado / convergencia del valle); el ruido solo perturbando la magnitud. Nota: la banda glaciar atada a `0.30-0.80·terrain_max` es una ELA estampada, no climática.
- **Estratos `sharpen_crags` (rank 6, `generator.cpp:306`):** remapeo periódico de la cota (`floor` a ~55 m). Derivar de un campo de resistencia/litología que el LEM y la erosión térmica consuman (erosión diferencial), o reducir a retoque muy local rompiendo la periodicidad vertical global.
- **Decide Rodrigo:** cuáles valen el esfuerzo vs. aceptarlos como cosmético declarado (opción legítima que respeta A.5 si se documenta).

### Mediums sueltos (fuera de las 3 direcciones)
- **Rank 18 — rayas de hierba/roca en vaguada (`generator.cpp:551`):** el `slope > rock_slope` binario es el único corte de material sin suavizado → las vaguadas de baja pendiente salen de hierba dibujando dedos verdes por las líneas de drenaje (**el residuo "rayas de hierba en ladera" del plan**). Sustituir por probabilidad/gradiente de roca vs hierba mezclando pendiente, curvatura y ruido isótropo (como el bosque usa densidad). *Esfuerzo medio; toca classify_materials.*
- **Rank 14 — árboles clonados por tipo (`models.cpp:121`):** wobble horneado una vez por tipo, VBO compartido → misma silueta clonada, diferenciada solo por escala/giro/brillo gris. Introducir variación de forma por instancia (deformar con seed de celda o pool de variantes de copa), jitter de tono verde, leve inclinación de tronco.
- **Ranks 23/24/25 (bajo impacto):** labio glaciar en isohipsa `ela-120`; treeline como offset fijo sin aspecto; snow_line que ignora `climate`. Todos muy amortiguados; candidatos a un segundo barrido barato.

---

## 3. Estado y próximo paso

- **Barrido 2026-07-09 commiteado** (build + 24/24 verdes): ranks 7(doc), 12, 13, 15, 16, 17, 21. Ver `plan-maestro.md` (nota de sesión) y commits `2747307…95f1d68`.
- **Falta el juicio visual in-game de Rodrigo** sobre el barrido (nieve en umbría, césped pardo, feathering de bosque, estratos, confeti). Si un fix no convence: `git revert <commit>` de ese solo.
- **Siguiente dirección: la elige Rodrigo** entre A (N2 costa), B (Agua+Luz C4) o C (depósito), con la profundidad que decida. Mi recomendación de ratio: **B (Agua+Luz)** es el mayor salto visual por esfuerzo y casi no toca generación; **A (N2)** es el arreglo estructural de más peso pero el más caro y arriesgado.
