#pragma once

// Geometría hidráulica de los cauces en unidades físicas absolutas (hallazgo
// #4 de docs/auditoria-2026-10/agua-costa.md). La anchura y la profundidad
// salen del caudal REAL de cada tramo, no de su fracción del río mayor.
//
// Cadena: área drenada A → caudal medio Q_m = A·P·C → caudal de cauce lleno
// Q_bf = R·Q_m → w(Q_bf), d(Q_bf).
//
// Fuentes:
// - Exponentes aguas abajo: Leopold & Maddock (1953), USGS PP-252:
//   w ∝ Q^0.5 y d ∝ Q^0.4.
// - Coeficientes: Hey & Thorne (1986), "Stable channels with mobile gravel
//   beds", J. Hydraul. Eng. 112(8), ríos de grava británicos (piedemonte
//   húmedo, régimen atlántico): w = 4.33·Q_bf^0.5 y d = 0.22·Q_bf^0.37 (m y
//   m³/s). Se toma su coeficiente de profundidad con el exponente 0.4 de
//   Leopold-Maddock: a Q_bf = 10 m³/s la diferencia es de un 7 %.
// - Tramos empinados: Finnegan et al. (2005), Geology 33(3): Manning con
//   relación anchura/calado fija da w = [n·α·(α+2)^(2/3)]^(3/8)·Q^(3/8)·
//   S^(-3/16). Se usa como techo de la anchura de régimen: con α = 25 y
//   n = 0.035 (grava) coincide con Hey & Thorne a Q = 10 m³/s y S = 0.5 %,
//   y estrecha las gargantas en vez del factor ad hoc 1/(1 + 6·S).
// - Clima: isla atlántica húmeda, P ≈ 1500 mm/año con un 70 % de
//   escorrentía (la evapotranspiración real ronda 450 mm/año).
// - Cauce lleno: en cuencas pequeñas y húmedas la crecida de cauce lleno
//   (periodo de retorno 1.5-2 años) es 10-20 veces el caudal medio; con el
//   método FEH, una cuenca de 20 km² y SAAR 1500 mm da QMED ≈ 14 m³/s frente a
//   Q_m ≈ 0.7 m³/s. Se toma R = 15.
//
// La lluvia orográfica del LEM no sale del LEM (lem.cpp no la exporta), así
// que la precipitación es uniforme en toda la isla.

#include <algorithm>
#include <cmath>

namespace terrain::hydraulics {

inline constexpr float kMeanPrecipitationM = 1.5f;  // m/año
inline constexpr float kRunoffCoefficient = 0.7f;
inline constexpr float kSecondsPerYear = 365.25f * 86400.0f;
inline constexpr float kBankfullToMeanFlow = 15.0f;

inline constexpr float kWidthCoeff = 4.33f;  // m·(m³/s)^-0.5
inline constexpr float kWidthExp = 0.5f;
inline constexpr float kDepthCoeff = 0.22f;  // m·(m³/s)^-0.4
inline constexpr float kDepthExp = 0.4f;

inline constexpr float kWidthDepthRatio = 25.0f;  // α de Finnegan
inline constexpr float kManningN = 0.035f;        // grava
inline constexpr float kFinneganQExp = 3.0f / 8.0f;
inline constexpr float kFinneganSExp = -3.0f / 16.0f;
// Por debajo de esta pendiente el techo de Finnegan no actúa (y se evita la
// singularidad de S → 0).
inline constexpr float kMinSlope = 1e-4f;

// Caudal medio anual (m³/s) de un área drenada en m².
[[nodiscard]] inline float mean_discharge_m3s(float area_m2) noexcept {
    return area_m2 * kMeanPrecipitationM * kRunoffCoefficient / kSecondsPerYear;
}

// Caudal de cauce lleno (m³/s): el que fija la forma del cauce.
[[nodiscard]] inline float bankfull_discharge_m3s(float area_m2) noexcept {
    return kBankfullToMeanFlow * mean_discharge_m3s(area_m2);
}

// Anchura de régimen de cauce lleno (m), Leopold-Maddock / Hey-Thorne.
[[nodiscard]] inline float regime_width_m(float q_m3s) noexcept {
    return kWidthCoeff * std::pow(std::max(q_m3s, 0.0f), kWidthExp);
}

// Calado medio de cauce lleno (m).
[[nodiscard]] inline float regime_depth_m(float q_m3s) noexcept {
    return kDepthCoeff * std::pow(std::max(q_m3s, 0.0f), kDepthExp);
}

// Anchura limitada por la pendiente (m), Finnegan et al. 2005.
[[nodiscard]] inline float slope_limited_width_m(float q_m3s,
                                                 float slope) noexcept {
    const float alpha = kWidthDepthRatio;
    const float shape = kManningN * alpha * std::pow(alpha + 2.0f, 2.0f / 3.0f);
    return std::pow(shape, kFinneganQExp)
         * std::pow(std::max(q_m3s, 0.0f), kFinneganQExp)
         * std::pow(std::max(slope, kMinSlope), kFinneganSExp);
}

// Anchura de cauce lleno del tramo (m): la de régimen, acotada en pendiente.
[[nodiscard]] inline float bankfull_width_m(float q_m3s, float slope) noexcept {
    return std::min(regime_width_m(q_m3s), slope_limited_width_m(q_m3s, slope));
}

} // namespace terrain::hydraulics
