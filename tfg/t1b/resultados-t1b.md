# T1b — Primer amplificador v0: la red quemada bate a la bicúbica

> **Ejecutado:** 2026-07-07 · `uv run python amplify/run_t1b.py` (cwd `tfg/`) · ~90 s por intento
> **Criterio (plan maestro C.4.1):** F1@10m de red > bicúbica en el par sintético; la elevación puede empeorar.
> **Resultado: SUPERADO** — y la elevación NO empeoró.

## Setup

Ventana 2048² (4.1×4.1 km) del MDT02 centrada en el máximo flujo acumulado (fila 2556, col 905 — valle principal). Par sintético: MDT02 → promedio a 30 m → amplificar de vuelta a 2 m. Umbral de red 0.5 km². Validación con `tfg/valida` (WhiteboxTools a 1 hilo — determinista).

## Resultado final (intento 6)

| Método | RMSE | NMAD | ME pendiente | **F1@10m** | F1@30m |
|---|---|---|---|---|---|
| Bicúbica | 2.01 m | 0.98 m | −1.24° | 0.5985 | 0.7714 |
| **v0 (red quemada + LEM canal)** | 2.04 m | 0.99 m | **−1.09°** | **0.6038** | 0.7677 |

## El método v0 que funcionó

1. Base bicúbica del DEM grueso.
2. Red D8 del grueso → **valley snapping**: cada nodo de la polilínea se ajusta al mínimo de la superficie bicúbica (disco ±7 celdas) y cada tramo se densifica con puntos re-ajustados cada ~4 celdas (siguen el meandro del fondo de valle). Información derivada SOLO del grueso.
3. Trinchera quemada (−1.5 m) como **condición Dirichlet** del LEM.
4. Uplift invertido de la SPL **sin normalizar**: U = (max(A₃₀,A₀)/A₀)^0.45 · S₃₀ ⇒ el estado estacionario reproduce las pendientes reales.
5. LEM Braun-Willett numpy (pointer doubling + procesado por niveles, sin numba) donde **solo evolucionan las celdas canal** (A_fino > A₀ = 22 500 m², Flint): las laderas conservan la curvatura bicúbica.

## Los 6 intentos (diagnóstico de cada fallo — material de memoria)

| # | Cambio | RMSE | F1@10m | Lección |
|---|---|---|---|---|
| 1 | U normalizado, red erosionable | 25.9 | 0.310 | U normalizado rompe la auto-consistencia: el LEM construye montaña |
| 2 | U sin normalizar, red Dirichlet | 19.4 | 0.128 | La red ya no se rellena, pero las laderas siguen desbocadas |
| 3 | A₀ de Flint (22 500 m², como `terrain/lem.cpp`) | 4.18 | 0.433 | Relieve controlado; el *faceting* de laderas destruye la curvatura que enruta tributarios |
| 4 | Solo evolucionan canales | 2.04 | 0.528 | Elevación clavada; la colocación ±30-60 m de la polilínea gruesa penaliza a 10 m |
| 5 | Valley snapping en nodos | 2.04 | 0.585 | Recupera casi toda la penalización |
| 6 | Densificación del tramo (anti corte de esquinas) | 2.04 | **0.604** | **Supera a la bicúbica** |

## Lectura honesta

- El margen es pequeño (+0.005): en el par sintético la bicúbica es un rival fuerte (interpola el MISMO sensor). El valor esperado de la constraint crece en el par real (E3: GLO-30 vs PNOA, sensores distintos) y con el detalle de T1c.
- Una sola ventana y una sola zona: E5 generaliza.
- Lo estructural ya está demostrado: **anclar la red observada no cuesta elevación** (2.04≈2.01) y el mecanismo completo (snap → burn → Dirichlet → SPL de canal) es estable y determinista.

## Siguiente (T1c)

Detalle emergente en laderas (erodibilidad K(p) con ruido multiescala + difusión) y anclaje de baja frecuencia (`z += blur_σ(upsample(z30) − z)`), manteniendo F1 ≥ 0.60 y empujando el RMSE/pendiente por debajo de la bicúbica (recuperar los −1.24° de suavizado).
