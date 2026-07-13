#pragma once

#include "terrain/generator.h"

#include <string>
#include <vector>

namespace ui {

// Minimapas de campos intermedios (C2 del plan): altura, caudal, clima,
// biomas y oleaje volcados a texturas GL con rampas de color simples, para
// inspección en la pestaña "Campos" del panel. Se reconstruyen al montar
// cada mundo; con la pestaña cerrada no cuestan nada por frame.
class FieldMaps {
public:
    FieldMaps() = default;
    ~FieldMaps();
    FieldMaps(const FieldMaps&) = delete;
    FieldMaps& operator=(const FieldMaps&) = delete;

    // (Re)construye las texturas desde los campos del mundo. Debe llamarse
    // en el hilo principal (contexto GL), en el montaje del mundo.
    void rebuild(const terrain::TerrainData& data);

    struct Map {
        std::string name;
        unsigned int texture = 0; // GLuint
    };
    const std::vector<Map>& maps() const { return maps_; }

private:
    void clear();
    std::vector<Map> maps_;
};

} // namespace ui
