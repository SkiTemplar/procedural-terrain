#pragma once

#include <istream>
#include <string>
#include <vector>

namespace app {

// Toma fija para capturas y medidas: pose de cámara con nombre. `alt` son
// metros sobre max(terreno, mar) en (x, z); `time_s` fija la fase del oleaje
// y del viento del césped (la misma toma da siempre la misma imagen).
struct Shot {
    std::string name;
    float x = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float alt = 0.0f;
    float time_s = 0.0f;
};

// Fotograma clave de un vuelo; `y` es la altura ABSOLUTA ya resuelta.
struct PathKey {
    float t = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

// Formato de tomas, una por línea (# comenta):  nombre x z yaw pitch alt [t]
// Lanza std::invalid_argument con el número de línea si una está mal formada.
std::vector<Shot> parse_shots(std::istream& in);

// Formato de vuelo, una clave por línea:  t x z alt yaw pitch
// (alt sobre el terreno, como en las tomas; `t` estrictamente creciente).
// Reutiliza Shot: name vacío, time_s = t de la clave.
std::vector<Shot> parse_flight(std::istream& in);

// Catmull-Rom uniforme por tramos sobre las claves (pasa por todas
// ellas; extremos con tangente por reflexión). El yaw se desenrolla para no
// girar 360° al cruzar ±180. t fuera de rango se satura a los extremos.
PathKey sample_path(const std::vector<PathKey>& keys, float t);

} // namespace app
