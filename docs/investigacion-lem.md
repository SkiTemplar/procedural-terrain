# Investigación: LEM estado del arte (2026-07-04)

Mejoras aplicables al solver stream-power de `src/terrain/lem.cpp`.
Las marcadas ✅ ya están implementadas (commit 5e61715).

## Implementado

- ✅ **Stack topológico de Braun & Willett 2013**: receiver → lista de donantes
  (CRS) → BFS desde base nodes. O(n) exacto por iteración, sin sort. El update
  implícito h = (h + U·dt + f·h_rcv)/(1+f) es exacto porque el receptor
  siempre se resuelve antes.
- ✅ **Calibración Flint independiente del grid**: pendiente de equilibrio
  S = kHeadwaterSlope·√(A0/max(A, A0)) con A0 = kChannelAreaM2 (~150 m de
  ladera sin canal). Sin esto la pendiente escalaba con la celda (cell/√A) y
  el relieve de equilibrio se hundía a ~200 m con macro fino: la isla salía
  plana por diseño (el uplift extra se erosiona; a estado estacionario la
  cota la fija la ley de pendiente, no el uplift total).
- ✅ **Uplift anisótropo** (Argudo/Schott 2023, mínimo esfuerzo): ridged con
  dominio comprimido ~3:1 a través de un eje fijado por seed → cordilleras
  lineales con espolones. Calibrado: media ~0.6, crestas → 1.

## Pendiente (por impacto/coste)

1. **Ruido alineado al flujo** en la amplificación macro→fino: sustituir/
   atenuar el fBM isótropo por ruido elongado a favor de −∇z (el campo de
   flujo del macro ya existe). Mata el "granulado que cruza los ríos".
   Grenier 2024 phasor noise: https://onlinelibrary.wiley.com/doi/10.1111/cgf.14992
2. **MFD linear-layout** (Anand 2020, https://arxiv.org/abs/1909.03176):
   receptores múltiples con pesos ∝ slope^p (p≈1.1 laderas, p→∞ canales).
   Elimina el zigzag diagonal del D8 y ensancha valles; reusa el mismo stack
   (DAG topológico). El update implícito pasa a
   h = (h + U·dt + Σ w_k·f_k·h_k)/(1 + Σ w_k·f_k).
3. **n > 1 (knickpoints nítidos)**: n∈[1.5, 2] con Newton-Raphson 2-3 iters
   por nodo (Braun 2023). Alternativa 80/20: realce de knickpoints en post
   (ya previsto en Agua v2 §cascadas).
4. **Basin graph de Cordonnier 2019** (https://esurf.copernicus.org/articles/7/549/2019/):
   grafo de cuencas + árbol de expansión mínima; estrategia carving (gargantas
   naturales) vs filling (lagos planos). Solo si las mesetas de lago molestan.
5. **Erosión de partículas como texturizador** del grid fino tras la bicúbica
   (Nick McDonald): barrancos que continúan el drenaje macro.

## Notas de calibración verificadas en runtime

- kDiffusion explícito 4-vecinos: límite de estabilidad 0.25 exacto; 0.20 deja
  margen (0.24 dejaba el modo checkerboard con amplificación -0.92).
- Epsilon de fill_pits: 0.0005 m/celda (con 0.01 las cuencas de 30-100 celdas
  acumulaban rampas de ~1 m que la bicúbica amplificaba).
- ridged() del proyecto: media ~0.5, crestas ~0.85 → smoothstep(0.30, 0.72)
  para máscaras de espinazo.
