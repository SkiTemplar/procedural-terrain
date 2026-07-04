# Investigación: Agua v2 (2026-07-04)

Técnicas y fuentes para la milestone "Agua v2", investigadas contra el estado
del arte. Contexto: isla 10×10 km, heightfield 4096² (~2.4 m/celda), red D8 +
flow accumulation + tallado de cauces + priority-flood ya existentes.

## Orden de implementación recomendado (dependencias)

1. **Efluentes de lagos** — lago como supernodo del grafo D8; salida por el
   saddle que ya da el priority-flood; breaching cosmético del labio (muesca
   1-2 celdas, pendiente monótona); Q_out = ΣQ_in. El río que sale del lago
   hereda el caudal acumulado.
2. **Anchura/profundidad por ley de potencia** (Leopold & Maddock): w = k·Q^0.4,
   d = k·Q^0.3 (con A como proxy de Q). Calibrar k para que el río mayor tenga
   la anchura deseada; todo lo demás escala solo. Sustituye al log actual.
3. **Perfil longitudinal monótono** + knickpoints declarados: fuerza z
   estrictamente decreciente y pendiente decreciente aguas abajo (S ∝ A^-0.45)
   EXCEPTO en knickpoints.
4. **Cascadas y rápidos**: perfil z(s) por río suavizado (~30-60 m); knickpoint
   donde S > 15-20 % o salto de S entre ventanas. Colapsar knickzone en:
   rellano superior + pared vertical de 1 fila + plunge pool (disco -1..-3 m,
   radio 1-2 anchuras). Saltos > ~12-15 m → partir en 2. Rápidos (2-10 %):
   flag whitewater para render + micro-escalones 0.5-1 m. Tipología Rosgen por
   pendiente (Peytavie 2019 "Procedural Riverscapes").
   Render low-poly: ribbon vertical, scroll de 2 ruidos, franjas de espuma
   (Cyanilux waterfall breakdown; RiME-style de Roodhuizen/Trümpler).
5. **Confluencias limpias**: doblar la entrada del afluente a ≤45-60° con
   Bézier (ángulo natural ~72° húmedo/45° árido); fillet de cauces; cama de la
   unión al mín de ambas.
6. **Deltas**: detectar boca deltaica (Q > p85 de la red, pendiente último
   tramo < 0.3 %, distancia al mar < 500 m; si costa abrupta → estuario).
   Esqueleto por bifurcación recursiva: brazos 100-300 m, p(bifurcar) 0.5-0.7,
   ángulo hijos 60-75°, w_hijo = w_padre/√2; lóbulo con smoothstep radial a
   nivel_mar + ε; islas interdistributarias emergen solas. Alternativa
   orgánica: mini-Seybold (PNAS 2007) en ventana 256².
7. **Islotes**: barras fluviales donde w > ~25-30 m y S 0.2-2 % (elipses
   gaussianas alineadas al flujo, λ = 6-10·w, pico = nivel_agua + 0.3-1 m,
   ensanchar cauce 20-40 % alrededor); barra de desembocadura frente a bocas;
   skerries/barras costeras según mapa de energía. Verificar tras estampar que
   el D8 del cauce sigue llegando al mar.
8. **Variedad de playa por energía de oleaje**: REI/fetch — 16 rayos por celda
   de costa (paso 4 celdas, cap ~2.5 km), ponderado por rosa de vientos del
   clima regional. E alto + pendiente alta → grava/acantilado; E alto + aporte
   fluvial → playa disipativa ancha; E bajo + aporte → marisma con charcas;
   E bajo sin aporte → roca. Anchura de compresión de costa = f(E, S) con blur
   1D a lo largo del contorno.
9. **Oxbows** (al final: muta la traza en planta): polilínea del tramo llano,
   migración Howard-Knutson (curvatura ponderada aguas arriba, kernel
   exponencial), cutoff por proximidad (< 1-2 anchuras entre nodos no
   adyacentes), tapones de sedimento en los extremos del bucle + point-bars.
   Código de referencia C++: aparis69/Meandering-rivers (SIGGRAPH Asia 2023);
   meanderpy como modelo.

Presupuesto estimado total: +2-5 s sobre el pipeline offline. Nada requiere GPU.

## Fuentes clave

- Génevaux 2013 Terrain Generation Based on Hydrology: https://hal.science/hal-01339224
- Peytavie 2019 Procedural Riverscapes: https://perso.liris.cnrs.fr/eric.galin/Articles/2019-riverscapes.pdf
- Paris 2023 Meandering Rivers (código): https://github.com/aparis69/Meandering-rivers
- Seybold delta model: https://arxiv.org/pdf/0711.3283
- Barnes priority-flood: https://arxiv.org/pdf/1511.04463
- Lindsay breaching-filling: https://jblindsay.github.io/ghrg/pubs/Lindsay-HP-preprint.pdf
- Leopold & Maddock PP-252: https://pubs.usgs.gov/publication/pp252
- Nick McDonald SimpleHydrology: https://nickmcd.me/2020/04/15/procedural-hydrology/
- mapgen4 rivers: https://simblob.blogspot.com/2018/10/mapgen4-river-representation.html
- Cyanilux waterfall shader: https://www.cyanilux.com/tutorials/waterfall-shader-breakdown/
- REI/fetch: https://www.ncbi.nlm.nih.gov/pmc/articles/PMC6298251/
