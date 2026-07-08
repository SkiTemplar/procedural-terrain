# Investigación — Naturalidad visual (silueta, islotes, picos, luz, agua)

> **Fecha:** 2026-07-08 · **Disparador:** feedback de Rodrigo — "las islas son muy forzadas", picos de un vértice en la superficie, luz y agua mejorables. **Regla del refocus:** este doc presenta opciones con trade-offs; **las decisiones las toma Rodrigo** (Protocolo Socrático). Nada se implementa sin su elección.
> Principio rector intacto: **"no fakear nada"** (plan §A.5) — cada mejora se propone como proceso o como reutilización de un campo físico ya simulado, nunca como estampado.

---

## 0. Diagnóstico en el código (verificado hoy)

| Síntoma | Causa raíz encontrada | Dónde |
|---|---|---|
| Silueta de isla "forzada" | La tierra es un **disco radial** (`continent = 1 − smoothstep(radio, 1, d)`) disfrazado con doble warp + lóbulos fbm. Un disco deformado sigue leyéndose como disco: costa "gorda", sin estructura de drenaje en la línea de costa. | `generator.cpp:44-73` (`coast_at`) |
| Islotes marinos "colocados" | Son una **banda de ruido estampada en un anillo** de distancia (d∈[0.78, 1.35]) con `continent = max(continent, satellite·0.5)`: tamaño uniforme, amplitud fija, viven a la misma distancia de la costa. Es exactamente el tipo de estampado que la filosofía A.5 prohíbe para el resto de rasgos. | `generator.cpp:62-68` |
| Picos de un vértice | Hipótesis rankeadas (ver §3): (1) ruido de detalle por vértice del `DetailSampler` en malla (hasta ±1.8 m en roca, tras TODA la erosión — nada lo suaviza); (2) `sharpen_crags` (bancos con saltos verticales) corre DESPUÉS de `smooth_peaks`; (3) depósito bilineal de las gotas. | `detail.cpp:85-102`, `generator.cpp:588-595` |
| Luz "plana" | No hay **sombras proyectadas** (el sol ilumina laderas que deberían estar en sombra de la montaña vecina), no hay **AO**, no hay tonemapping. Lo que sí hay y está bien: hemisferio cielo/suelo, sol cálido, blended flat shading, niebla exponencial. | `terrain_shaders.h:62-97` |
| Agua igual en río y mar | Mar y ríos comparten shader y **las mismas 2 senoidales** de vértice; no hay color por profundidad real, ni espuma de orilla, ni dirección de flujo en ríos. Lo que sí está bien: Fresnel de Schlick, especular solar, alpha por ángulo. | `terrain_shaders.h:131-189`, `mesher.cpp` |

Campos físicos **ya calculados** que se pueden reutilizar gratis para naturalidad (sin fakear, porque el rasgo visual queda correlacionado con el proceso): `wave_energy` (REI/fetch por celda de costa), rosa de vientos (`wind_dir_deg/focus`), dirección de flujo D8 y `water_level` (hidrología), eje tectónico `theta` (uplift anisótropo), `climate`.

---

## 1. Silueta de la isla principal — 3 opciones

**Por qué canta:** en la naturaleza la línea de costa no es el borde de una función de distancia: es el **contorno de nivel z = 0 de un paisaje erosionado** que se hunde en el mar. Rías, cabos, bahías y penínsulas son valles y crestas del relieve, ahogados por el nivel del mar. Nuestro pipeline ya tiene el 90% de la maquinaria (uplift anisótropo + LEM); lo que falla es que la forma dominante viene del disco, no del relieve.

### Opción A — Maquillar la máscara actual (coste: 1 sesión, riesgo BAJO)
Más octavas de lóbulos, warp anisótropo alineado con `theta`, radio dependiente del sector. Referencia: las funciones de conformación de [Red Blob Games](https://www.redblobgames.com/maps/terrain-from-noise/) (el propio autor declara la aproximación "bastante limitada": produce bandas de elevación alineadas con la distancia).
- ✅ Barato, no toca tests, mejora algo.
- ❌ No ataca la raíz: sigue siendo un disco disfrazado. Los islotes seguirían estampados.

### Opción B — Costa emergente del LEM (coste: 2-4 sesiones, riesgo MEDIO) ⭐ recomendada por encaje filosófico
La continentalidad deja de ser LA forma y pasa a ser un **prior débil de uplift** (dónde puede haber tierra, con sus lóbulos y el eje tectónico); el LEM corre con el dominio extendido **bajo el nivel del mar** (fondo marino real, no caída analítica), y la costa ES el contorno h=0 del relieve erosionado. Las bahías salen de los valles ahogados (rías), los cabos de las divisorias, y los **islotes emergen solos**: picos del mismo campo de uplift que apenas asoman (skerries) — se elimina el estampado del anillo satélite.
- ✅ Es la respuesta "no fakear" de libro: la silueta sale del mismo proceso que el resto del relieve. Islotes gratis y coherentes (§2). La caída al fondo marino, hoy analítica (`h −= drop·sea_depth`), pasa a ser relieve simulado.
- ❌ Regenera todos los mundos (hash de tests cambia, justificable en el commit); hay que conservar el invariante "el mar rodea el dominio" como prior de borde (la máscara radial no muere: se degrada a condición de contorno débil); tuning de cuánta tierra sale (el área de isla ya no es un slider directo — `island_radius` pasa a escalar el prior).
- Nota: la infraestructura del acantilado por sectores (`cs.cliff`, frente dentado) sobrevive — se re-expresa sobre la costa emergente.

### Opción C — B + retroceso costero simulado (coste: +2-3 sesiones sobre B, riesgo ALTO)
Añadir erosión marina de verdad: donde `wave_energy` es alta y la roca resiste, el mar socava (muesca al nivel del mar → colapso → **acantilado activo con plataforma de abrasión** a sus pies). Es el mecanismo real de los sea cliffs ([formación de plataformas de abrasión](https://www.sciencedirect.com/topics/earth-and-planetary-sciences/wave-cut-platform)); daría acantilados *donde el oleaje los tallaría*, no donde un ruido lo marca.
- ✅ El acantilado deja de ser un sector de ruido (`cliff_raw` fbm) y pasa a ser consecuencia del oleaje ya simulado. Máxima coherencia física.
- ❌ Investigación + implementación no trivial (iteración retroceso/colapso/talud); puede desestabilizar la costa de A1 (playas por energía). Es un "Agua v3".

---

## 2. Islotes marinos

- Con la **Opción B**, los islotes salen gratis y naturales: lóbulos secundarios del prior de uplift bajo el mar cuyos picos asoman tras LEM + inundación. Cadenas alineadas con `theta` (como los archipiélagos reales siguen sus zonas de subducción/hotspots — [formación de archipiélagos](https://www.oreateai.com/blog/the-formation-of-archipelagos-natures-island-chains/46118ac12b7686f3c1c04699604a782f)) y tamaños con distribución natural (los que casi no asoman son rocas/skerries).
- Con la **Opción A**, lo máximo honesto es modular el anillo actual por profundidad del fondo y alinear los islotes con el eje tectónico — mejora, pero sigue siendo estampado.

---

## 3. Picos de un vértice — diagnóstico ANTES de fix

Hipótesis rankeadas (todas verificables en <1 sesión):

| # | Hipótesis | Verificación barata |
|---|---|---|
| H1 | **Ruido de detalle en malla** (`DetailSampler::height`, detail.cpp:85): en roca `roughness=1.5` × `detail_amplitude_m=1` × (0.75·fbm30 + 0.35·fbm7 + 0.08·perlin2.2) ≈ hasta ±1.8 m por vértice, aplicado en el mesher DESPUÉS de toda la erosión — ningún proceso lo relaja. | Poner `detail_amplitude_m=0` en ImGui y mirar: si los picos desaparecen, es H1. |
| H2 | **`sharpen_crags`** (generator.cpp:257): bancos estratificados con saltos verticales, corre DESPUÉS de `smooth_peaks` — en crestas de 1 celda el salto puede leerse como aguja. | Desactivar la llamada (generator.cpp:595) y regenerar. |
| H3 | **Depósito de gotas** (erosion.cpp:47): `deposit_at` bilineal puede apilar sedimento en la misma celda ([van der Veen: limitar el sedimento soltado por posición evita spikes](https://medium.com/@ivo.thom.vanderveen/improved-terrain-generation-using-hydraulic-erosion-2adda8e3d99b)); la erosión sí usa kernel 3×3 anti-agujas, el depósito no. | Contador de picos sobre el heightfield ANTES del mesher: celda con `h > max(8 vecinos) + 2·cell·talus` → si hay pocos, el problema no está aquí sino en H1/H2. |

**Fix natural según culpable:** H1 → amortiguar el detalle por curvatura local o pasar el grano fino a normal/color (no geometría), o relajarlo con un mini-talus en el sampler; H2 → suavizar el borde del banco 1 celda; H3 → repartir depósito 3×3 como ya hace la erosión (proceso real: el sedimento no cae en un punto). Referencia general térmica/talus: [Jákó 2011, erosión hidráulica+térmica en GPU](https://old.cescg.org/CESCG-2011/papers/TUBudapest-Jako-Balazs.pdf), [SoilMachine de Nick McDonald](https://nickmcd.me/2022/04/15/soilmachine/).

---

## 4. Luz más realista (sin romper el low-poly)

Ordenado por salto de realismo/esfuerzo. Los dos primeros son los que separan "render de juguete" de "paisaje":

| Mejora | Qué aporta | Coste | Notas |
|---|---|---|---|
| L1 · **Sombras del sol** (shadow map, 2-3 cascadas CSM) | Las montañas proyectan sombra sobre valles y laderas — el mayor salto de realismo exterior que existe. Hoy una ladera opuesta al sol solo se oscurece por su propia normal. | 2-3 sesiones (bias/acne/peter-panning, cascadas) | [LearnOpenGL Shadow Mapping](https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping). Con flat shading el acné es más benigno (caras grandes). |
| L2 · **AO precomputado por vértice** desde el heightfield (horizon-based, offline, determinista) | Valles, gargantas y pies de acantilado se asientan; barato en runtime (1 float por vértice). Encaja con el pipeline CPU offline. | 1-2 sesiones | [AO de baja frecuencia para terreno](https://mtnphil.wordpress.com/2014/04/11/low-frequency-ambient-occlusion/), [AO sobre heightmap](https://medium.com/@edinspace/adding-ambient-occlusion-to-a-terrain-map-484743b669d9). Muestrear horizonte en 8-16 direcciones a varias distancias. |
| L3 · **Tonemapping + gamma** (ACES/filmic + exposición) | Los cielos dejan de quemar, las sombras conservan color; requisito para que L1/L2 luzcan. | 0.5 sesión | Post-proceso trivial en el frag final o framebuffer HDR. |
| L4 · **Cielo con gradiente + disco solar + niebla con altura** | Horizonte creíble, atardeceres; la niebla exponencial pura delata "esfera de color". | 1 sesión | Gradiente cenit→horizonte + `exp(-h)` en la niebla. |

Trade-off global: L1 es lo único que toca la arquitectura del render (framebuffer de profundidad, pases); L2 encaja en la generación (campo nuevo tipo `wave_energy`); L3/L4 son shader-only.

---

## 5. Agua (mar y ríos, distintos porque lo son)

Reutilizando lo ya simulado — cada efecto anclado a un campo físico:

| Mejora | Mecanismo | Campo que reutiliza | Coste |
|---|---|---|---|
| W1 · **Color por profundidad real** | El agua conoce el fondo: `depth = water_level − h(fondo)` por vértice de la malla de agua → someras turquesa/transparentes, profundas oscuras. Técnica estándar de agua estilizada ([Alisavakis](https://halisavakis.com/my-take-on-shaders-stylized-water-shader/), aquí con la ventaja de tener el heightfield real, sin depth-buffer tricks). | heightfield + water_level | 1 sesión |
| W2 · **Espuma de orilla por oleaje** | Franja de espuma cuya anchura/intensidad = `wave_energy` de esa costa (barlovento espumoso, sotavento calmo). Nadie más tiene este campo: es nuestro diferencial "no fake". | `wave_energy` | 1 sesión |
| W3 · **Ríos con flujo** | Las olas del río se desplazan A LO LARGO del cauce (dirección D8 por vértice), amplitud ∝ pendiente (rápidos agitados, remansos lisos); el mar deja de compartir senoidales con los ríos. | dirección D8 + pendiente | 1-2 sesiones |
| W4 · **Mar con oleaje direccional** | 2-3 olas Gerstner low-poly orientadas con la rosa de vientos (`wind_dir_deg/focus`) — el mar se mueve HACIA donde el oleaje ya golpea según A1. Coherencia total con las playas. | rosa de vientos | 1 sesión |

---

## 6. Paquetes propuestos (para que Rodrigo elija y ordene)

- **P0 · Picos** — diagnóstico H1-H3 + fix del culpable. (≈1-2 sesiones; es el único con sabor a bug)
- **P1 · Costa natural** — Opción A, B o C de §1 (islotes incluidos). (1 / 2-4 / 5-7 sesiones)
- **P2 · Luz** — L1+L2+L3 (+L4). (3-5 sesiones)
- **P3 · Agua** — W1+W2+W4 (+W3). (3-4 sesiones)

Interacción con el plan: todo esto vive en la **Fase C4 (pulido visual)** del plan maestro, salvo P1-B/C que son generación (pre-C). A2 (oxbows) sigue pendiente y es independiente. El plan actual dice A2 → B → C; si la naturalidad pesa más que los oxbows, es legítimo reordenar — decisión de Rodrigo.

## Fuentes

- [Red Blob Games — Terrain from noise (island shaping y sus límites)](https://www.redblobgames.com/maps/terrain-from-noise/)
- [Wave-cut platforms (ScienceDirect Topics)](https://www.sciencedirect.com/topics/earth-and-planetary-sciences/wave-cut-platform) · [Formación de archipiélagos](https://www.oreateai.com/blog/the-formation-of-archipelagos-natures-island-chains/46118ac12b7686f3c1c04699604a782f)
- [Jákó 2011 — Fast Hydraulic and Thermal Erosion on the GPU](https://old.cescg.org/CESCG-2011/papers/TUBudapest-Jako-Balazs.pdf) · [van der Veen — Improved terrain generation using hydraulic erosion](https://medium.com/@ivo.thom.vanderveen/improved-terrain-generation-using-hydraulic-erosion-2adda8e3d99b) · [Nick McDonald — SoilMachine](https://nickmcd.me/2022/04/15/soilmachine/)
- [LearnOpenGL — Shadow Mapping](https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping) · [Low-frequency AO para terreno](https://mtnphil.wordpress.com/2014/04/11/low-frequency-ambient-occlusion/) · [AO sobre heightmap](https://medium.com/@edinspace/adding-ambient-occlusion-to-a-terrain-map-484743b669d9)
- [Harry Alisavakis — Stylized water shader (profundidad + espuma)](https://halisavakis.com/my-take-on-shaders-stylized-water-shader/)
