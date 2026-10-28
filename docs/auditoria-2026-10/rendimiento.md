# Auditoría de rendimiento por código — 2026-10-29

Alcance: lectura estática de `src/` y `CMakeLists.txt`, sin compilar ni ejecutar. Las cifras
marcadas con «est.» salen de contar operaciones, vértices y bytes en el código, no de un perfilado;
cada fila de la tabla indica cómo medirla de verdad antes de tocar nada.

Datos de partida (plan §C3, 2026-07-13, Release): 114,6 fps en panorámica a 2500 m (1414/4096
chunks) y 126,0 fps a ras de bosque (2673/4096); generación 4096² en 18 s Release y ~150 s Debug.

## 0. Veredicto

- **Generación (CPU)**: bien encaminada. Los bucles por celda independientes ya van con
  `std::execution::par`. Lo serie que queda es intrínsecamente secuencial (gotas, Braun-Willett,
  priority-flood, D8 en orden de cota) o barato. 18 s < objetivo de 30 s: **para el jugador no hace
  falta optimizarla**. Lo que sí duele es el bucle de desarrollo en Debug (150 s), y eso se
  arregla con flags, no con algoritmos.
- **Runtime**: el número medido **no demuestra margen**. `glfwSwapInterval(1)` (`src/core/window.cpp:35`)
  activa la vsync y el panel muestra `1/dt` instantáneo (`src/ui/debug_ui.cpp:47`). Las dos muestras,
  114,6 y 126,0, oscilan en torno a 120: es la firma de un tope de vsync a 120 Hz con jitter de
  `dt`. El coste real del frame puede ir de 2 a 8,3 ms y hoy no hay forma de saberlo.
  Además, las poses fueron estáticas, y el problema probable está en el vuelo: la reconstrucción de
  chunks y de césped es síncrona en el hilo principal (picos de frame, no media baja).
- **Lo que viene** (triplanar PBR, sombra cercana, árboles con tarjetas alfa) cae justo sobre los dos
  puntos débiles: densidad de triángulos de terreno muy por encima de lo necesario y vegetación sin
  frustum culling ni LOD. Hay margen de sobra si se arreglan esos dos puntos antes de añadir coste.

## 1. Tabla ranqueada

Ámbito: **G** = CPU-gen, **F** = CPU-frame, **GPU**. Riesgo para el determinismo:
**ninguno** (no toca la generación), **bit-exacto** (toca la generación y conserva los bits),
**nuevo golden** (resultado determinista, pero distinto del actual: hay que regenerar los hashes una vez).

| # | Problema / oportunidad | Fichero:línea | Ámbito | Ganancia estimada | Esfuerzo | Riesgo determinismo | Cómo medirlo | Veredicto |
|---|---|---|---|---|---|---|---|---|
| 1 | Medición tapada por la vsync y fps instantáneo; sin timers GPU ni p99/máximo | `src/core/window.cpp:35`, `src/ui/debug_ui.cpp:47` | todos | 0 directa; sin esto no se puede decidir nada | S | ninguno | `GL_TIME_ELAPSED` por pase (doble búfer de queries), vsync conmutable, media/p99/máx en ventana de 1 s | **hacer primero** |
| 2 | Rebuild de chunk síncrono en el hilo principal: nivel 0 = 513² = 263 k vértices × 44 B = 11,6 MB VB + 6,3 MB IB; índices generados en serie; `glBufferData` + `glGen*`/`glDelete*` por rebuild; hasta 2 por frame | `src/terrain/chunked_terrain.cpp:46-102`, `:168-186`, `:223-306`; `chunked_terrain.h:32` | F | est. 15-25 ms por chunk de nivel 0 y 4-6 ms por chunk de nivel 1 → a 90 m/s entran ~1,8 chunks/s en nivel 0 y ~4,4/s en nivel 1, así que habrá tirones. Hilo de fondo + presupuesto de subida: picos < 2 ms | M | ninguno (la malla es función pura de chunk y nivel) | ms por rebuild y nivel, bytes subidos por frame, máx/p99 de frame en vuelo a 90 y 450 m/s | **hacer** |
| 3 | LOD por distancia **2D al centro** del chunk: a 2500 m de altura los chunks bajo la cámara van a nivel 0 (0,3 m de celda donde 1 px ≈ 3,9 m). Además, cada anillo termina en triángulos subpíxel (tabla §2.3) | `chunked_terrain.cpp:151-166`, `chunked_terrain.h:37-40` | GPU + VRAM + F | est. triángulos residentes 13,4 M → ~3 M (÷4), VRAM de terreno ~470 MB → ~110 MB, rebuilds ÷3 | S (distancia 3D/AABB) · M (umbrales SSE + geomorph) | ninguno (visual: pops; lo decide Rodrigo) | triángulos enviados por pase, timer GPU del pase de terreno, VRAM con `GL_NVX_gpu_memory_info` | **hacer** |
| 4 | Vegetación sin frustum culling (solo distancia), celdas de 512 m, 2600 m de alcance y modelo completo a cualquier distancia. Un pino a 2600 m mide ~4 px y se pinta con ~60 triángulos | `src/veg/veg_renderer.cpp:13`, `:275-297`; `src/ui/debug_ui.h:29` | GPU (vértice) + F (draws) | hoy est. 20-45 M tri/frame si hay 300-500 k árboles en rango (confirmar con la línea `vegetacion:` del log). Con frustum ÷3; con LOD + impostores ÷10 o más | M-L | ninguno (la colocación no cambia) | Σ `index_count × instances` por pase y timer GPU del pase de vegetación | **hacer antes de los árboles nuevos** |
| 5 | Césped reconstruido en serie en el hilo principal cada 24 m: ~6 k celdas, hasta 55 k instancias y 6 Perlin 3D por brizna; crea VAO y 3 búferes nuevos (vuelve a subir el modelo) | `src/veg/grass_field.cpp:15-37`, `:91-182` | F | est. 5-15 ms por rebuild → ~4 tirones/s a 90 m/s. Asíncrono o por bandas: < 1 ms | S-M | ninguno (concatenando por filas en orden) | ms de `GrassField::rebuild` y máx de frame | **hacer** (junto con #2) |
| 6 | Orden de pases: cielo a pantalla completa primero, terreno antes que la vegetación (overdraw del terreno bajo las copas). Hoy es barato; con triplanar PBR, caro | `src/main.cpp:338-348`, `:368-389` | GPU | hoy < 0,3 ms est.; con PBR, 20-40 % del pase de terreno en bosque | S | ninguno | `GL_SAMPLES_PASSED` y timer del pase de terreno con el orden actual y el nuevo | **hacer con el PBR** |
| 7 | 2,7 k draws de terreno + 1-2 k de vegetación por frame; un VAO+VBO+EBO por chunk, con bind/unbind en cada draw | `src/render/mesh.cpp` (`draw`), `main.cpp:371-373` | F (driver) | est. 1-3 ms de CPU; el pase de sombra lo duplicaría. Con MDI: 1 draw por nivel | M | ninguno | timer CPU alrededor del bloque de draws + timestamp GPU | condicional: hacer si #1 muestra > 2 ms de CPU en draws o al añadir la sombra |
| 8 | Vértice de 44 B en float (pos, color, normal, luz); índices duplicados por chunk aunque la topología es idéntica por nivel | `src/render/mesh.h:11-16`, `chunked_terrain.cpp:60-99` | VRAM + subida | ÷1,8 en VB y –100 % en IB (compartido) | S | ninguno | bytes por rebuild | **hacer** (dentro de #2) |
| 9 | No hay golden de generación a 4096²: el test solo compara dos ejecuciones del mismo binario; los presets del repo no guardan `height_hash` | `tests/terrain_tests.cpp:427-443`, `presets/*.json` | G | prerrequisito de #11-#16 | S | — | test con hashes fijos de height, material, water_level, flow, forest_density y shadow (512² y 4096², 2 semillas) | **hacer antes de tocar la generación** |
| 10 | Debug a 150 s: `terrain_gen` compila con `/Od /RTC1` sin flags por target | `CMakeLists.txt:60-66`, `CMakePresets.json` | G (desarrollo) | est. 150 s → 30-50 s (`/O2` para `terrain_gen` en Debug) o ~20 s (preset RelWithDebInfo) | S | posible diferencia de bits Debug≠Release ya hoy (el vectorizador de `/O2` usa rutas vectoriales de `pow`/`exp`): el golden debe fijarse por configuración | tiempo por fase (#11) | **hacer** (mejora de calidad de vida, no de rendimiento) |
| 11 | Tiempo por fase de generación no instrumentado (solo el total) | `src/terrain/generator.cpp:1040-1067` | G | 0; dice dónde están los 18 s | XS | ninguno | `steady_clock` en el lambda `report` | hacer junto con #9 |
| 12 | LEM: `pow(max(uplift,0.05), 0.3)` es invariante y se recalcula en cada celda e iteración (150 × 1 M) | `src/terrain/lem.cpp:363-364` | G | est. 0,5-1,5 s | XS | **bit-exacto** (misma función, mismo argumento) | fase «relieve (LEM)» | barato: hacer si se toca el LEM |
| 13 | LEM: pasos 2-4 (stack, área, solver implícito) en serie sobre 1024² × 150 iteraciones | `lem.cpp:296-374` | G | est. 2-4 s (paralelo por cuenca; Amdahl por la cuenca mayor) | M | **bit-exacto** si cada cuenca conserva su subsecuencia del BFS global (ver §3.6) | fase LEM + golden | no hace falta (gen < 30 s); opcional |
| 14 | Hidrología: `std::sort` de 16,7 M índices con comparador indirecto; D8 + acumulación en serie | `src/terrain/hydrology.cpp:132-188` | G | est. 1-2 s | S | **nuevo golden**: los empates de `filled` (frecuentes en planos rellenados con ε) cambian el orden de suma de `flow` | fase «hidrologia» | no hace falta; si se hace, clave total `(filled desc, idx asc)` + sort paralelo |
| 15 | Priority-flood con `std::priority_queue<Cell>` + `vector<bool>`: 2 veces sobre 16,7 M en hidrología y 16 veces sobre 1 M en el LEM | `hydrology.cpp:41-84`, `:129`, `:252`; `lem.cpp:65-115`, `:399-401` | G | est. 2-4 s | M | **nuevo golden** (la versión con cola FIFO de Barnes cambia el orden de reclamación de celdas en los planos) | fase «hidrologia» | no hace falta |
| 16 | Stencils serie paralelizables: `smooth_peaks`, `clamp_spikes`, pase de llanura+fbm de hidrología, suavizado de lámina, suavizado final del LEM y erosión térmica (con gather en el mismo orden de suma) | `src/terrain/erosion.cpp:141-246`; `hydrology.cpp:103-127`, `:679-711`; `lem.cpp:407-418` | G | est. 0,5-1 s en total | S | **bit-exacto** (Jacobi puros; la térmica exige reproducir el orden de las sumas) | golden | no hace falta; opcional |
| 17 | Montaje del mundo en el hilo principal: colocación de vegetación (16,7 M celdas en serie), `build_water_mesh` en serie y 4096 chunks de nivel 4 → ventana congelada al terminar cada bake | `main.cpp:278`; `veg_renderer.cpp:61-228`; `src/terrain/mesher.cpp` | F (una vez) | est. 1-3 s de congelación → < 0,5 s | S-M | ninguno / bit-exacto si se concatena por filas en orden | timer alrededor de `std::make_unique<World>` | condicional (solo molesta al regenerar) |
| 18 | `DetailSampler::height` evalúa 6 Perlin **3D** con y=0 por vértice y por brizna | `src/terrain/detail.cpp:121-135`, `noise.cpp:34-63` | F (rebuilds) | est. –40 % del coste por vértice de los niveles 0-1 y del césped | S | ninguno para el hash; cambia la superficie de detalle (colisión, cota de las instancias) | ms por rebuild de nivel 0 | decisión de Rodrigo (cambio visual leve) |
| 19 | `/arch:AVX2`, `/GL`+`/LTCG`, OpenMP | `CMakeLists.txt` | G | 5-15 % est. | S | AVX2: **nuevo golden** y binario no portable; `/fp:fast`: **prohibido** (rompe la regla de bits) | — | **no hace falta** |
| 20 | `glGetUniformLocation` en cada `set_*` (~25 llamadas por frame) | `src/render/shader.cpp:70-86` | F | < 0,05 ms | XS | ninguno | — | **no hace falta** |
| 21 | Culling CPU de 4096 AABB y vector `stale` por frame | `chunked_terrain.cpp:171-221` | F | < 0,1 ms | — | ninguno | — | **no hace falta** |
| 22 | Mar: plano de 40 km con shader por píxel y blending encima de un fondo ya sombreado (overdraw ×2 en mar) | `main.cpp:392-420`, `terrain_shaders.h:404-470` | GPU | est. < 1 ms a 1600×900 | S | ninguno | timer del pase de agua | **no hace falta** salvo que el timer pase de 1,5 ms |
| 23 | Penacho de desembocaduras O(costa × bocas) | `src/terrain/coast.cpp:162-181` | G | despreciable (ya va en paralelo) | — | — | — | **no hace falta** |
| 24 | Erosión por gotas (400 k × 40 pasos) en serie | `erosion.cpp:82-139` | G | est. 0,5-1 s; solo paralelizable por teselas con calendario fijo | M | **nuevo golden** | fase «erosion hidraulica» | **no hace falta** |

Desglose estimado de los 18 s (por conteo, sin perfilar; #11 lo confirmaría): LEM ~5-8 s,
hidrología + riverscape ~5-7 s, gotas + térmica + stencils ~1,5-2,5 s y el resto, que ya va en
paralelo, ~2-3 s.

## 2. Los 5 primeros, con diseño

### 2.1 (#1) Medición honesta antes de optimizar

1. **Vsync conmutable** desde el panel (`glfwSwapInterval(0/1)`) y, en el modo de medida, fps
   como media de una ventana de 1 s más p99 y máximo de `dt` (búfer circular de 240 muestras).
2. **Timers GPU por pase**: un par de `GL_TIME_ELAPSED` por pase (cielo, terreno, vegetación,
   césped, agua, ImGui) con 3 juegos de queries en anillo, y lectura del juego de hace 2 frames
   con `GL_QUERY_RESULT_AVAILABLE`. Así nunca se bloquea: nada de `glFinish` ni de `glGetQueryObject`
   sobre la query del mismo frame.
3. **Timers CPU** (`steady_clock`) en `ground.update`, en cada `rebuild` (con nivel y bytes),
   en `grass.update` y en el bloque de draws.
4. **Contadores**: draws y triángulos enviados por pase (Σ `index_count/3 × instancias`).
5. **Benchmark reproducible**: `--bench <seed>` recorre una spline fija (costa → valle → cumbre →
   panorámica) a 90 m/s y a 450 m/s (boost ×5) durante 60 s y vuelca un CSV con frame, cpu_ms,
   gpu_ms por pase, rebuilds y bytes. Hoy las poses son estáticas y no ven los picos de streaming.

Criterio para cerrar C3 con datos: p99 < 16,6 ms y GPU total < 8 ms en el benchmark, con la
vsync apagada.

### 2.2 (#2 + #5 + #8) Streaming de chunks y césped fuera del hilo principal

Hoy `rebuild()` genera los vértices en paralelo, pero el hilo principal **espera**, construye
1,57 M índices en serie (nivel 0), sube 18 MB y crea y destruye objetos GL.

Diseño:

- **Trabajador de fondo** (uno basta; dentro puede seguir usando `std::execution::par`). La cola de
  peticiones `(chunk, nivel, epoch)` se ordena por prioridad (distancia ponderada por el error en
  pantalla) y el trabajador produce `std::vector<PackedVertex>`. `DetailSampler`, `Palette` y
  `TerrainData` son de solo lectura durante el juego, así que son thread-safe tal como están.
  Los resultados vuelven al hilo principal por una cola con mutex.
- **Subida con presupuesto**: el hilo principal sube como máximo N MB por frame (p. ej. 4 MB)
  a un **pool de VBO por clase de nivel**, reutilizados con `glBufferSubData` (o un anillo
  persistente con `GL_MAP_PERSISTENT_BIT` y fences). Se acaba el `glGen`/`glDelete` por rebuild.
  La malla vieja sigue visible hasta que la nueva está subida: nunca hay huecos.
- **IB compartido por nivel**: la rejilla y el faldón tienen la misma topología en todos los chunks
  completos del mismo nivel; los chunks de borde (63 celdas, porque 4095 = 63·64 + 63) usan un IB
  propio. Se eliminan 6,3 MB por chunk de nivel 0 y el bucle serie de índices.
- **Vértice compacto de 24 B**: posición `3×f32`, normal `2×snorm16` (octaédrica), color
  `RGBA8` (o, con PBR, `material_a, material_b, peso, forest_density` en `4×u8`), luz `2×unorm8`
  + 2 B de relleno. Ocupa ÷1,8 respecto a hoy.
- **Histéresis de LOD**: bajar de nivel solo al superar el umbral × 1,1. Evita el ping-pong en
  la frontera.
- **Césped**: la misma cola. Además, rejilla de **teselas de 32 m** alrededor de la cámara: al
  moverse solo se generan las teselas nuevas del borde (una banda) y las instancias van a un
  único búfer de instancias con slots por tesela (`glBufferSubData` del slot). Con frustum por
  tesela, el radio de 108 m se puede mantener.

Determinismo: no se toca la generación. La malla de un chunk es función pura de `(chunk, nivel)`
y no depende del hilo ni del orden de llegada.

Criterio de aceptación: en el benchmark, ningún frame > 2 ms de CPU dedicado a streaming y
p99 < 10 ms a 90 m/s.

### 2.3 (#3) LOD por error en pantalla y distancia 3D

Tamaño de píxel con FOV vertical de 70° y 900 px de alto: `m/px = d × 2·tan(35°)/900 = d × 0,001556`.

| Nivel | Arista | Umbral actual | Arista en px al final del anillo | Umbral para ~3 px (900p) | Ídem a 1440p |
|---|---|---|---|---|---|
| 0 | 0,305 m | 250 m | 0,78 px | 65 m | 105 m |
| 1 | 0,61 m | 600 m | 0,65 px | 131 m | 209 m |
| 2 | 2,44 m | 1800 m | 0,87 px | 523 m | 837 m |
| 3 | 9,77 m | 4200 m | 1,5 px | 2,09 km | 3,35 km |
| 4 | 19,5 m | resto | — | resto | resto |

Por debajo de ~2-3 px de arista la GPU sombrea cuádruples 2×2 medio vacíos y el rasterizador se
satura. Los anillos actuales acaban todos en triángulos subpíxel.

Cambios:

1. **Distancia 3D al punto más cercano del AABB** (no 2D al centro). Arregla el caso de los
   2500 m de altura y garantiza nivel 0 bajo los pies aunque la cámara esté en la esquina del chunk.
2. **Umbrales derivados de la altura del viewport** (`edge_L / (k_px × 2·tan(fov/2) / H)`),
   recalculados al cambiar de resolución, con `k_px` como parámetro del panel.
3. Estimación con las dos medidas anteriores: triángulos residentes de 13,4 M a ~3 M y VRAM de
   terreno de ~470 MB a ~110 MB.

Decisión de Rodrigo (protocolo socrático), porque cambia lo que se ve:

- **A — Solo umbrales y distancia 3D**. Esfuerzo S. Con chunks de 156 m el nivel 0 sigue
  siendo grueso (hasta 4 chunks de 524 k triángulos alrededor de la cámara) y aparecen pops en
  la silueta.
- **B — A + geomorphing** en el vertex shader (interpolar hacia la altura del nivel padre en el
  último 20 % del anillo). Esfuerzo M y sin pops. Requiere guardar la altura del padre en el vértice.
- **C — Quadtree CDLOD** (nodos de tamaño variable con un IB por nivel). Esfuerzo L. Es lo
  óptimo en triángulos y draws, pero es una reescritura del terreno.

### 2.4 (#4) Vegetación: culling, LOD e impostores antes de enriquecer los árboles

Estado: 400 celdas de 512 m con hasta 6 lotes cada una, culling solo por distancia y el mismo
modelo de 60-92 triángulos a 5 m y a 2600 m. Con árboles más ricos (cientos o miles de
triángulos y tarjetas alfa) esto escala de forma lineal y no cabe.

Diseño en dos escalones:

1. **Escalón CPU (S-M)**: celdas de 128 m con AABB (altura mínima y máxima de sus instancias),
   frustum culling con el mismo test Gribb-Hartmann del terreno y 3 LOD por especie:
   - LOD0 (< 150 m): modelo rico con tarjetas alfa;
   - LOD1 (150-600 m): malla simplificada sin tarjetas (copa cerrada, ~30-60 triángulos);
   - LOD2 (600-2600 m): **impostor**, un quad orientado a cámara que muestrea un atlas de
     8-16 vistas horneado al cargar (render-to-texture del LOD0: albedo + normal en un array 2D).
   Cada celda guarda las instancias ordenadas por especie y el LOD se elige por celda. Es
   suficiente porque la celda (128 m) es pequeña frente a los anillos.
2. **Escalón GPU (M-L)**, si el escalón 1 no basta: un compute shader recorre todas las instancias
   (SSBO), hace frustum y distancia por instancia y escribe en un búfer de instancias por
   (especie, LOD) con `atomicAdd`, más `DrawElementsIndirectCommand`. Después, un
   `glMultiDrawElementsIndirect` por pase. El coste de CPU queda prácticamente a cero y el pase
   de sombra reutiliza el mismo culling con el frustum de la luz.

Tarjetas alfa: con MSAA 4× ya activo (`window.cpp:26`), usar **alpha-to-coverage**
(`GL_SAMPLE_ALPHA_TO_COVERAGE`) en lugar de blending; dibujar las tarjetas después de los
opacos; mipmaps del alfa con preservación de cobertura, para que las copas no se deshagan a
distancia.

Medida: triángulos enviados en el pase de vegetación y su timer GPU. Objetivo: < 3 M triángulos
de vegetación por frame y < 3 ms de GPU.

### 2.5 (#6 + presupuesto) Encaje de triplanar PBR y sombra cercana por encima de 60 fps

Presupuesto orientativo a 60 fps (16,6 ms), que hay que validar con #1:

| Pase | Objetivo GPU |
|---|---|
| Terreno PBR | ≤ 3 ms |
| Vegetación (con tarjetas) | ≤ 3 ms |
| Sombra cercana | ≤ 1,5 ms al re-renderizar; ~0,2 ms de media si se cachea |
| Césped | ≤ 1 ms |
| Agua + cielo + ImGui | ≤ 2 ms |
| Margen | ≥ 6 ms |

Diseño:

- **Orden de pases**: opacos de vegetación (de cerca a lejos por celda) → terreno → césped →
  tarjetas alfa → agua → **cielo al final** con `gl_Position.z = gl_Position.w` y
  `GL_LEQUAL`, para que solo sombree los píxeles sin geometría. Si en bosque denso el pase de
  terreno sigue alto, se añade un depth prepass del terreno (solo posición, barato) y el pase
  de color con `GL_EQUAL`.
- **Triplanar barato**:
  - **biplanar** (dos proyecciones dominantes, técnica de Quílez) en vez de tres;
  - si `|n.y| > 0,8` (casi todo el terreno transitable), solo la proyección superior;
  - como mucho 2 materiales por píxel (par dominante + peso, desde el vértice compacto de §2.2);
  - `GL_TEXTURE_2D_ARRAY` para albedo y normal (BC7 y BC5) con mipmaps y anisotrópico 8×;
  - más allá de ~300 m, sin muestreo: color macro del vértice (la rama es coherente por
    distancia, así que no diverge).
  No hace falta bindless: los arrays de texturas cubren el caso con un solo bind.
- **Sombra cercana de 250 m**: el sol es fijo y el mundo, estático. Basta un shadow map de 2048²
  ajustado a la rejilla de texels, que **solo se re-renderiza cuando la ventana ajustada se
  desplaza** (cada ~16-32 m de movimiento): el coste medio es casi cero. Proyectores de sombra:
  vegetación a < 250 m (LOD1 o el mismo culling GPU) y terreno en **nivel 2**, nunca en 0 o 1
  (la auto-sombra del terreno ya está horneada en `data.shadow`). El césped no proyecta. Filtro:
  PCF de 3×3 con fundido hacia la sombra horneada entre 200 y 250 m.
- **MDI del terreno** (#7): con el IB compartido por nivel y un VBO grande por clase de nivel,
  el terreno pasa de ~2,7 k draws a un `glMultiDrawElementsIndirect` por nivel. Con la sombra,
  esto evita duplicar el coste de driver.

## 3. Notas de detalle

### 3.1 Generación: qué ya es paralelo

En paralelo: el prior, la litología y el uplift (`generator.cpp:196-316`), la amplificación
(`:341-378`), el detalle de laderas, `sharpen_crags`, `shape_coast`, `classify_materials`,
`compute_lighting`, el D8 y la difusión del LEM (`lem.cpp:267-292`, `:377-395`), toda la
exposición de costa (`coast.cpp`) y los vértices de chunk.

En serie: el resto de pasos del LEM, las gotas, la térmica, los stencils de `erosion.cpp` y casi
toda la hidrología y el riverscape (recorridos en orden de cota y talla con escritura en vecindario).

### 3.2 `std::function` y virtuales

Solo en el callback de progreso (una llamada por fase). No hay `std::function` ni virtuales en
bucles internos. Las lambdas de `std::sort` y de `for_each` se inlinean.

### 3.3 Memoria

A 4096² cada campo `float` ocupa 67 MB. `fill_depressions` copia la altura y crea un
`priority_queue` que puede alcanzar decenas de MB (12 B por celda). `build_water_mesh` reserva
`vertex_of` de 67 MB. El pico de RAM rondará los 1-1,5 GB. No es un problema medido; se
anota por si se sube la resolución.

### 3.4 Flags

Hoy: Release `/O2 /Ob2`, Debug `/Zi /Ob0 /Od /RTC1`, sin flags por target.

- Para el bucle de desarrollo (#10): o un preset `RelWithDebInfo-OpenGL`, o, en Debug,
  `target_compile_options(terrain_gen PRIVATE $<$<CONFIG:Debug>:/O2 /Ob2>)` quitando `/RTC1`
  de `CMAKE_CXX_FLAGS_DEBUG` (`/RTC1` es incompatible con `/O2`).
- No cambiar `_ITERATOR_DEBUG_LEVEL` solo en `terrain_gen`: rompe el enlace con el ejecutable, los
  tests y las librerías de vcpkg (LNK2038).

### 3.5 Riesgo latente de portabilidad de bits

El vectorizador de MSVC a `/O2` puede llamar a rutas vectoriales de `pow`/`exp` que despachan
según la CPU. El mismo binario podría dar bits distintos en otra máquina. No es un problema
mientras el juego se genere y se juegue en la misma máquina, pero el golden de #9 conviene
fijarlo por configuración (Debug y Release por separado) y documentarlo.

### 3.6 LEM en paralelo por cuenca (#13), por qué es bit-exacto

El stack de Braun-Willett se construye con un BFS FIFO global desde todos los nodos base. Los
nodos de una cuenca solo se encolan al procesar nodos de esa misma cuenca, así que la subsecuencia
de cada cuenca en el stack global es idéntica a la de un BFS de esa cuenca sola.

En consecuencia, el orden de las sumas `area[receiver] += area[i]` y el del solver implícito
(cada celda solo lee su receptor) se conservan si cada cuenca se procesa entera por un hilo.
La ganancia la limita la cuenca mayor; en el mar hay muchísimos nodos base triviales.
