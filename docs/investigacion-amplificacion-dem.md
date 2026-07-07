# Investigación — Amplificación de DEMs reales con constraints hidrológicas

> **Generado:** 2026-07-07 · informe de viabilidad del **trabajo del año / candidato a TFG**
> **Decisión de origen:** elegido entre 10 ideas orientadas a Shield AI / Aechelon (2026-07-06 → decisión 2026-07-07).
> **Perfil objetivo:** Ingeniero de Gráficos → Aechelon Technology (generación de imagen en tiempo real, bases de datos geo-específicas).
> **Estado:** VIABLE con encuadre corregido (ver §3). Referencias verificadas contra la web en julio 2026.

---

## 1. Problema y propuesta

**Problema industrial (el de Aechelon, literal):** los DEMs globales gratuitos (SRTM, Copernicus GLO-30) tienen 30 m/celda. Un simulador de vuelo o un motor de exploración a pie necesita detalle sub-métrico a 2 m. La interpolación (bicúbica, kriging) sobre-suaviza; la super-resolución neuronal inventa microrelieve sin garantías físicas: la red de drenaje resultante deja de ser la real.

**Propuesta:** amplificar DEMs reales de 30 m a ~2 m usando el **LEM stream-power (Braun & Willett 2013, O(n)) ya implementado en este motor** como prior físico, con una **constraint dura**: la red de drenaje extraída del DEM real se impone como restricción (stream burning sobre las condiciones del LEM), de modo que el detalle emerge de la simulación de erosión pero los ríos amplificados son *los ríos reales*. El producto se valida con **métricas hidrológicas cuantitativas** contra LiDAR de alta resolución (PNOA) y se **renderiza en tiempo real** en el motor OpenGL existente.

Es la evolución natural del principio rector del proyecto ("no fakear nada"): datos reales + procesos simulados, nunca detalle pintado.

**Pipeline (las 3 etapas = el producto de Aechelon en miniatura):**

```
[Datos fuente reales]          [Generación de base de datos]         [Render tiempo real]
Copernicus GLO-30 (30 m)  →   1. extraer red de drenaje + anclas  →  motor actual:
(+ PNOA MDT02 como             2. upsample a malla 2 m                chunks + LOD +
 ground truth, nunca           3. LEM fino con uplift/erodibilidad    flat shading +
 como entrada)                    derivados del DEM + constraint      vegetación
                                  de red (stream burning)
                               4. validación hidrológica (D8)
                               5. export al formato del motor
```

Esbozo del núcleo (paso 3): estimar un campo de uplift de equilibrio local desde la topografía observada (U ≈ K·Aᵐ·Sⁿ con A y S del DEM grueso — la misma stream-power law al revés), correr el LEM en la malla fina con ese uplift + erodibilidad modulada por ruido (el detalle emerge del proceso), fidelidad de baja frecuencia anclada al DEM de entrada (downsample(salida) ≈ entrada) y la red de drenaje real quemada como condición de contorno. Re-validar D8 al final, igual que hace hoy `riverscape`.

## 2. Estado del arte (verificado)

### 2.1 Amplificación procedural/geomorfológica (computer graphics)

| Referencia | Qué hace | Qué le falta |
|---|---|---|
| Guérin, Digne, Galin, Peytavie (EG 2016) — *Sparse representation of terrains* | Amplificación por diccionario de landforms | Sin física ni hidrología |
| Cordonnier et al. (EG 2016) — *Terrain Generation from Tectonic Uplift and Fluvial Erosion* | Uplift + stream-power → redes dendríticas emergentes | Genera desde cero, no amplifica DEM real |
| **Schott, Galin, Guérin, Paris, Peytavie (SIGGRAPH 2024) — *Terrain Amplification using Multi-scale Erosion*** | **EL COMPETIDOR MÁS CERCANO** (leído completo, ver §3.1): amplifica LR→HR "hidrológicamente consistente" con stream-power acotado + termal + deposición, multi-escala, GPU interactivo | Stream-power "fantasma" sin uplift con parámetros estéticos (no LEM calibrado); sin constraint de la red real (su anclaje es el complementario: crestas); validación solo interna (volumen de breaching), **nunca contra ground truth real**; límite 8192² en una GPU |
| Schott et al. (TOG 2023) — *Interactive Erosion Simulation* | Authoring en dominio de uplift, stream-power interactivo | Authoring, no amplificación condicionada a DEM real |
| Argudo, Andújar, Chica (Vis. Comput. 2017) — *Coherent multi-layer landscape synthesis* | Síntesis multicapa por ejemplares reales | Patch-based, sin física |
| Guérin et al. (EG 2022) — *Gradient Terrain Authoring* | Dominio del gradiente + Poisson, GPU tiempo real | Sin LEM ni hidrología |
| Tzathas, Gailleton, Steer, Cordonnier (EG 2024) — *Analytical erosion* | Solución analítica de stream-power ("slider de tiempo") | Envejecimiento, no amplificación condicionada |

### 2.2 Super-resolución de DEMs con deep learning

- **Argudo, Chica, Andújar (EG 2018)** — FCN guiada por ortofoto; **benchmark Pyrenees & Tyrol** (DEM 2 m del ICGC, pares LR/HR listos). El antecedente directo del "condicionamiento por imagen"; sin garantía hidrológica.
- **GATA (Zhao et al., SIGGRAPH Asia 2019, EA)** — GAN multi-tema para amplificación; artefactos que rompen el drenaje. Código público.
- **D-SRGAN (2021)**, **TfaSR (ISPRS 2022)**, **transformer a gran escala (ISPRS 2023)**, benchmarks multimodales 2025-2026 (GDEMSR, DEM-OPT-Depth). Todos miden RMSE de elevación/pendiente; **ninguno valida la red de drenaje ni usa prior físico**.

### 2.3 LEM e inversión (geomorfología)

- **Braun & Willett (2013)** — el solver O(n) implícito que ya está en `terrain/lem` (núcleo de FastScape).
- **Croissant & Braun (2014)**, **Barnhart et al. (2020, Landlab/terrainbento)**, método adjunto (ESurf 2025), inversión bayesiana (Chandra et al. 2019) — la comunidad **ya invierte LEMs contra DEMs reales**, pero siempre para inferir tectónica/parámetros, a la resolución del DEM. **Nadie usa la inversión como mecanismo de super-resolución.**

### 2.4 Consistencia hidrológica como constraint

- **HydroSHEDS** (Lehner et al.) — el canon de "DEM hidrológicamente condicionado" (stream burning global); condiciona, no amplifica.
- **Stream burning / breaching** (Lindsay 2016, WhiteboxTools) — el mecanismo clásico para imponer una red dada: candidato directo para nuestra constraint.
- Verificado: existen PINNs/SR con física para fluidos, pero **no existe super-resolución de DEM con pérdida/constraint explícita de red de drenaje**. Hueco abierto.

## 3. GAP y novedad (análisis honesto)

**No se puede reclamar** "amplificación de terreno hidrológicamente consistente" a secas: **Schott et al. 2024 (SIGGRAPH) ya usa esa frase casi literal**. La línea Cordonnier→Schott (LIRIS/Adobe) es dueña de ese titular.

### 3.1 Schott et al. 2024 en detalle (paper completo leído el 2026-07-07; PDF en `tfg/papers/`)

**Qué hace exactamente.** Pipeline multi-escala `T_m = A_{m-1}∘…∘A_0(T_0)` donde cada paso `A_k` = upsample bicúbico ×2/×4 → erosión fluvial → estabilización termal → deposición de sedimentos, desde 128-256² hasta 8192² (×32). La erosión fluvial es stream-power **sin uplift** (`u=0`, solo talla), explícita e iterativa, con dos modificaciones de diseño: *clamping* `ẽ = min(sⁿ, sⁿmax)·min(aᵐ, aᵐmax)` para evitar incisiones puntiagudas, y erodibilidad variable `k(p) = k(1−ρ(p))` con mapa de dureza ρ (ruido fractal + edición del artista) para romper la auto-similitud. Routing de flujo **multi-dirección** (Holmgren, exponente p=1.3) con **una sola iteración de actualización del drenaje por iteración de erosión** (aproximación GPU heredada de Schott 2023). Parámetros estéticos, no calibrados: n=2, m=0.8 (m/n=0.4 cuando la geomorfología usa ~0.5), k=5·10⁻⁴, miles de iteraciones por escala. Dos post-procesos clave: **retargeting por difusión** (detecta crestas/picos como celdas con drenaje `a < a₀=2`, calcula el error de cota respecto al input y lo difunde con ~500 iteraciones de Laplaciano para restaurar picos sin dañar la erosión) y **multi-scale breaching** (variante de Barnes 2014: breaching parcial suavizado con radios decrecientes `r_{k+1}=0.5·r_k`, paso final r=1 → garantiza drenaje completo sin cañones de 1 celda). GPU compute (GLSL): 5-11 ms por iteración a 4096²; retargeting+breaching en CPU, segundos. Código público: `github.com/H-Schott/MultiScaleErosion` (C++/GLSL).

**Correcciones a lo que asumíamos:** (1) NO es offline — la amplificación es interactiva en GPU; nuestra ventaja no puede venderse como "tiempo real vs offline". (2) SÍ amplifican DEMs reales de baja resolución (Figura 15 del paper, un terreno real de 128²) — pero sin anclarse a la red real y sin comparar contra datos reales de alta resolución.

**Lo que confirma el paper (los huecos, ahora verificados):**
- **Su "consistencia hidrológica" es interna, no fiel a datos**: significa "el agua drena sin pozos" (breaching lo garantiza tallando donde haga falta), no "los ríos están donde están los ríos reales". Nada impide que la red amplificada se mueva de valle.
- **Su anclaje a datos es el complementario exacto del nuestro**: el retargeting preserva la cota de **crestas y picos** del input; nadie ancla la **geometría de la red de drenaje** a la observada. Nuestra constraint dura sigue libre.
- **Su validación cuantitativa es una sola métrica interna**: volumen medio de breaching b̄ (material que hay que quitar para que drene libre, Tabla 3 del paper), comparada contra StyleGAN/procedural/sparse — **nunca contra un DEM real de alta resolución**. Ni error de elevación, ni posición de la red, ni Strahler. Nuestro E0 ya mide más que el paper.
- **Su limitación declarada es nuestra Fase 2**: dicen textualmente que pasar de 8192² exige descomponer en parches con "propagación del drenaje entre fronteras" como reto abierto — exactamente el problema de tiling que nuestro motor de chunks + el TFG por celdas debe resolver.

**Lo que conviene REUTILIZAR de Schott 2024 en el TFG:**
1. **Multi-scale breaching** — nuestra constraint por stream burning talla trincheras de 1 celda (el artefacto exacto que su breaching multi-escala elimina); adoptar su suavizado de radios decrecientes sobre la trinchera quemada la hace geomorfológicamente plausible.
2. **Retargeting por difusión** — sirve tal cual para nuestro anclaje de baja frecuencia (fidelidad al DEM grueso) y para imponer cotas de la red real (constraints con δ de datos + difusión).
3. **La métrica b̄** — añadirla a la suite E0 (barata y permite comparar números con el paper).
4. **Su código como baseline E1b** — correr MultiScaleErosion sobre nuestro GLO-30 y evaluarlo con nuestro pipeline E0 contra el MDT02: sería **la primera evaluación cuantitativa de Schott 2024 contra terreno real**, y mediría exactamente cuánto pierde la amplificación sin anclaje a datos (hipótesis: F1@10m de la red no mejora sobre bicúbica aunque el terreno "parezca" real).

**La combinación que NO existe publicada** (y que es este TFG; ejes afinados tras leer el paper completo, §3.1):

1. **LEM geomorfológico calibrado sobre datos reales** (Braun-Willett O(n) implícito, uplift/erodibilidad estimados desde el DEM observado) — en Schott la erosión es stream-power sin uplift con parámetros estéticos (m/n=0.4, k de diseño), sin calibración a datos.
2. **Constraint dura de la red de drenaje observada**: la red del DEM real se impone (stream burning en las condiciones del LEM); en Schott la consistencia es interna (drena, pero no necesariamente por donde drenan los ríos reales) y su anclaje a datos es el complementario del nuestro (crestas, no red).
3. **Validación cuantitativa contra ground truth real** (elevación + derivados + IoU de red, Strahler/Horton, hipsometría, y también su b̄): Schott solo reporta una métrica interna (volumen de breaching) y nunca compara con un DEM real de alta resolución; la familia DL mide solo RMSE. Esto solo ya es contribución metodológica — incluida la primera evaluación de Schott 2024 contra datos reales (experimento E1b).
4. **Escala por celdas/tiles con drenaje coherente entre fronteras e integración en motor caminable**: la limitación declarada de Schott (8192² máximo en una GPU, "propagación del drenaje entre parches" como reto abierto) es exactamente lo que el pipeline por chunks del motor + el TFG por celdas debe resolver.

**Título de trabajo (encuadre corregido):** *"Super-resolución de DEMs reales mediante un modelo de evolución del paisaje como prior físico, con la red de drenaje observada como restricción dura y validación hidrológica cuantitativa"*.

**Riesgo de ser pisado:** LIRIS/Adobe y la comunidad FastScape (que ya colaboran — Tzathas 2024) están a una iteración del acoplamiento "LEM real + DEM observado". Mitigación: los ejes 2 y 3 son los menos triviales de replicar; documentar y versionar el trabajo desde el principio (el repo con historial es evidencia de prioridad), y considerar un paper corto/workshop a mitad de curso si los resultados acompañan.

## 4. Datos

### 4.1 Entrada (baja resolución)

- **Copernicus GLO-30** (fuente principal): 30 m, el más moderno y homogéneo. Descarga: Copernicus Data Space, OpenTopography, AWS Open Data (bulk sin registro). Licencia gratuita con atribución DLR/Airbus/ESA. **Ojo:** la edición DGED 2023_1 rellenó los **Pirineos españoles** con DEM de alta resolución — verificar la procedencia de las celdas usadas o elegir también zonas fuera del relleno para no contaminar el experimento.
- Alternativas para test de robustez: **NASADEM** (SRTM reprocesado, dominio público), ASTER GDEM v3 (más ruidoso — fuente "difícil"), ALOS AW3D30.
- **Caveat estructural:** los globales son **DSM** (superficie); el ground truth PNOA es **DTM** (terreno desnudo) → sesgo sistemático por vegetación/edificios. Tratarlo: zonas de escasa cobertura vegetal, o comparar también contra el MDS del PNOA, o citar FABDEM como fuente corregida.

### 4.2 Ground truth (alta resolución) — PNOA-LiDAR (IGN/CNIG)

| Producto | Resolución | Origen | RMSE Z |
|---|---|---|---|
| **MDT02** | **2 m** | 2ª cobertura (2015-2021, 0.5-4 pts/m²) | — |
| MDT05 | 5 m | 1ª cobertura (2009-2015, 0.5 pts/m²) | 40 cm |
| 3ª cobertura (LAZ) | 5 pts/m² + RGBI | 2022-2025 | 10 cm |

- Licencia **CC-BY 4.0 compatible** (gratis, uso comercial con atribución IGN). Formato .asc / **COG GeoTIFF**, ETRS89 UTM.
- Descarga: Centro de Descargas del CNIG, familia LIDAR (`centrodedescargas.cnig.es`).
- Internacionales si hacen falta más zonas: USGS 3DEP 1 m (dominio público), swissALTI3D 0.5 m, UK EA LIDAR 1 m (OGL), Dinamarca DHM 0.4 m.

### 4.3 Benchmark publicado directamente relevante

- **Pyrenees & Tyrol (Argudo et al. 2018)** — DEM 2 m (ICGC) + ortofotos, ~22K tiles de train / 11K de validación, 4 zonas de test (Bassiero, Forcanada, Dürrenstein, Monte Magro). **Es Pirineos = nuestra zona**; permite comparar números contra literatura publicada. Distribución: grupo ViRVIG/UPC (reconstruible desde ICGC si el enlace cae).

### 4.4 Diseño de pares

1. **Sintético (control):** MDT02 → downsample a 30 m → amplificar → comparar contra el MDT02 original. Mide la capacidad del método en condiciones limpias.
2. **Real (el examen de verdad):** GLO-30 → amplificar → comparar contra MDT02 en las mismas celdas. Más difícil (gap DSM/DTM + diferencias de sensor); es lo que casi ningún paper de SR hace.

## 5. Métricas de evaluación

**De elevación:** RMSE y MAE (estándar de la literatura) + **NMAD** (Höhle & Höhle 2009, robusta a colas no gaussianas — el estándar de calidad de DEM) + percentiles LE90/LE95.

**De derivados:** RMSE de pendiente (amplifica el ruido de alta frecuencia — discrimina el over-smoothing de la bicúbica), aspecto (circular, ojo con 0/360°), curvatura. Análisis espectral (PSD) / variograma para juzgar si la textura reconstruida tiene la energía correcta en cada frecuencia — capta lo que el RMSE no ve.

**Hidrológicas (la aportación diferencial):** mismo pipeline D8 (fill/breach → flow accumulation → umbral → red) aplicado idénticamente a baseline, método y ground truth. Comparar:
- **IoU de la red de drenaje** (con buffer de tolerancia).
- Distancia media/Hausdorff entre cauces extraídos y de referencia.
- **Strahler/Horton**: nº de cauces por orden, razón de bifurcación, longitudes por orden.
- **Curva e integral hipsométrica** por cuenca (fácil de calcular, muy interpretable en defensa).
- **Volumen medio de breaching b̄** (Schott et al. 2024, §6.5): material a retirar para que el terreno drene libre, dividido por el área. Métrica interna barata que permite comparar números directamente con el paper competidor.

**Geomorfológicas (perceptuales):** agreement de clasificación de **geomorphons** (Jasiewicz & Stepinski 2013; `r.geomorphon` de GRASS) — matriz de confusión / kappa entre landforms del amplificado y del real.

## 6. Baselines y herramientas

- **Baselines obligatorios:** bicúbica (canon), y un neural reproducible (FCN de Argudo con su benchmark, o EDSR/D-SRGAN con código público).
- **Extracción de drenaje para validación:** WhiteboxTools (Rust, rápido) o pysheds (Python, pipeline reproducible); TauDEM y GRASS (r.stream.order para Strahler, r.geomorphon) como complemento. GDAL (`gdaldem`) para slope/aspect/TRI.
- **Lectura de datos en el motor:** GeoTIFF/COG → o bien GDAL como dep de una herramienta CLI de preproceso (fuera del motor, mantiene el motor limpio), o lector COG mínimo propio. Decisión de diseño pendiente (ver §8).

## 7. Plan de experimentos (borrador)

| # | Experimento | Pregunta que responde |
|---|---|---|
| E0 | Reproducir pipeline de validación sobre datos crudos (GLO-30 vs MDT02, bicúbica) | ¿Funciona la instrumentación? Números de referencia |
| E1 | Sintético: MDT02→30 m→amplificar vs MDT02 | ¿El LEM-prior mejora a bicúbica y a un neural en elevación/derivados? |
| E1b | **Schott 2024 como baseline**: correr `MultiScaleErosion` (código público) sobre GLO-30 y evaluarlo con el pipeline E0 | Primera evaluación cuantitativa de Schott contra terreno real; hipótesis: sin anclaje a datos, su F1@10m de red no mejora a la bicúbica aunque el relieve "parezca" real |
| E2 | Hidrológico: métricas de red en E1 | ¿La constraint dura produce redes medibles mejor que la consistencia libre? (el resultado central del TFG) |
| E3 | Real: GLO-30→amplificar vs MDT02 | ¿Sobrevive el método al gap DSM/DTM y de sensor? |
| E4 | Ablación: sin constraint de red / sin anclaje de baja frecuencia / solo ruido | ¿Qué aporta cada pieza? (defensa ante tribunal) |
| E5 | Zonas: 2-3 tipos de relieve (alpino pirenaico, colinas, mesetas) | Generalización |
| E6 | Render: la zona amplificada volando en el motor a >60 fps | La demo Aechelon (y captura para la memoria) |

**MVP del TFG:** una zona pirenaica, E0-E2 + E6, baseline bicúbica. Todo lo demás es ampliación. Con el MVP ya hay memoria defendible: método + validación cuantitativa + demo en tiempo real.

## 8. Encaje con el plan maestro (`docs/plan-maestro.md`)

- **Fases A-C (cierre de la isla) siguen intactas y ganan valor:** son el motor y el pipeline de render que el TFG reutiliza (chunks+LOD, riverscape, validación D8 ya existente). Cerrar `v1.0-isla` antes de abrir el track TFG.
- **Track TFG = rama nueva** (p. ej. `tfg-amplificacion`) tras el tag `v1.0-isla`, con módulos nuevos: `tools/dem_prep` (CLI con GDAL: descarga/recorte/reproyección, fuera del motor) y `terrain/amplify` (uplift inverso + LEM fino + constraint de red — reutiliza `lem`, `hydrology`, `riverscape`).
- **Fase 2 (planeta) muta de opción:** puede pasar de "planeta procedural" a **"la Tierra real como planeta caminable"** consumiendo celdas amplificadas — whole-earth geo-específico, el producto literal de Aechelon. Decisión en D0, no ahora; ambas siguen abiertas.
- **Determinismo:** misma seed + mismo DEM de entrada ⇒ misma amplificación. El test de hash existente se extiende al pipeline nuevo.

## 9. Riesgos

| Riesgo | Prob. | Mitigación |
|---|---|---|
| LIRIS/Adobe o FastScape publican el acoplamiento LEM+DEM observado | Media | Ejes 2-3 (constraint dura + validación cuantitativa) son los menos replicables; historial git como evidencia; workshop paper a mitad de curso |
| Gap DSM/DTM contamina E3 | Alta (seguro que aparece) | Tratarlo explícitamente: zonas poco vegetadas + discusión en la memoria (es un hallazgo, no un fallo) |
| El uplift inverso no converge a relieve plausible | Media | Fallback por capas: anclaje de baja frecuencia + LEM solo como generador de residuo de alta frecuencia (sigue siendo novedoso por los ejes 2-3) |
| Coste computacional del LEM a 2 m en zonas grandes | Baja | El solver es O(n); 4096² ya corre en el motor. Tiling con solape para zonas mayores |
| Relleno de alta resolución de GLO-30 2023_1 en Pirineos | Media | Verificar procedencia de celdas; añadir zona de control fuera del Pirineo |
| Scope creep del TFG | Alta | MVP de §7 cerrado por escrito con el tutor; el resto es "trabajo futuro" |

## 10. Próximos pasos

1. **Validar el encuadre con un tutor potencial** (departamento de gráficos/GIS) llevando este informe — el TFG necesita tutor y este documento es la propuesta.
2. ~~E0: montar el pipeline de validación y sacar los números de referencia de la bicúbica.~~ **HECHO (2026-07-07)** — pipeline en `tfg/e0/` (uv + rasterio + WhiteboxTools, corre en ~40 s), resultados en `tfg/e0/resultados-e0.md`. Números a batir (Benasque/Posets, GLO-30 bicúbica vs MDT02): RMSE 8.39 m, NMAD 3.16 m, pendiente −2.0° de aplanado sistemático, **F1 de red de drenaje 0.52 @ 10 m** (0.81 @ 30 m) y colapso del orden 4 de Strahler. El hueco del TFG está en la tolerancia fina, tal y como se hipotetizaba.
3. ~~Leer a fondo Schott et al. 2024.~~ **HECHO (2026-07-07)** — análisis detallado en §3.1, ejes de novedad afinados, PDF de autor en `tfg/papers/`. Salidas accionables: adoptar su multi-scale breaching y su retargeting por difusión como componentes del método propio; añadir su métrica b̄ a la suite; nuevo experimento E1b (su código público como baseline evaluado contra ground truth real, cosa que el paper nunca hace).
4. Mientras tanto: cerrar Fases A-C del plan maestro (el motor que el TFG necesita).

## 11. Fuentes principales

**Computer graphics:** Guérin 2016 `hal.science/hal-01258986` · Cordonnier 2016 `doi/10.1111/cgf.12820` · **Schott 2024 `dl.acm.org/doi/10.1145/3658200`** · Schott 2023 `dl.acm.org/doi/10.1145/3592787` · Argudo 2017 `doi/10.1007/s00371-017-1393-6` · Gradient Terrains 2022 `doi/10.1111/cgf.14460` · Tzathas 2024 `doi/10.1111/cgf.15033`

**DL super-resolution:** Argudo 2018 `doi/10.1111/cgf.13345` · GATA 2019 `dl.acm.org/doi/10.1145/3355089.3356553` · D-SRGAN `arxiv.org/pdf/2004.04788` · TfaSR ISPRS 2022 `S0924271622001332` · Transformer ISPRS 2023 `S1569843223003205`

**Geomorfología/LEM:** Braun & Willett 2013 `S0169555X12004618` · Croissant & Braun 2014 `esurf.copernicus.org/articles/2/155/2014` · Barnhart 2020 `doi/10.1029/2018JF004961` · FastScape `fastscape.org`

**Hidrología/validación:** HydroSHEDS `hydrosheds.org` · Höhle & Höhle 2009 (NMAD, ISPRS J.) · Geomorphons `S0169555X12005028` · WhiteboxTools `whiteboxgeo.com/manual` · TauDEM `hydrology.usu.edu/taudem` · pysheds `mattbartos.com/pysheds`

**Datos:** Copernicus DEM `dataspace.copernicus.eu` (+ OpenTopography, AWS Open Data) · PNOA-LiDAR `pnoa.ign.es/pnoa-lidar/productos-a-descarga` · Centro de Descargas CNIG `centrodedescargas.cnig.es` (familia LIDAR) · licencia IGN `ign.es/resources/licencia/Condiciones_licenciaUso_IGN.pdf`

---

> *Informe vivo. Actualizar tras la lectura completa de Schott et al. 2024 y tras E0. La decisión de si este trabajo es formalmente el TFG (o proyecto paralelo) se cierra con el tutor.*
