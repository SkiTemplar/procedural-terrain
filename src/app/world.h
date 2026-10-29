#pragma once

#include "render/mesh.h"
#include "terrain/chunked_terrain.h"
#include "terrain/detail.h"
#include "terrain/generator.h"
#include "terrain/water_surface.h"
#include "terrain/mesher.h"
#include "veg/grass_field.h"
#include "veg/veg_renderer.h"

#include <glad/glad.h>

#include <optional>

namespace app {

// Pose de cámara por CLI o por fichero de tomas: x, z en mundo, yaw/pitch en
// grados y alt en metros sobre max(terreno, mar) en (x, z).
struct CameraPose {
    float x = 0.0f;
    float z = 0.0f;
    float yaw = -90.0f;
    float pitch = -12.0f;
    float alt = 180.0f;
};

// C4-W1/W2: textura RG del dominio para el shader del MAR — r = profundidad
// real del fondo bajo el nivel del mar (m), g = energía de oleaje [0,1]. Da
// color por calado (Beer-Lambert) y espuma de orilla POR PÍXEL, cosa que los
// vértices del plano del mar (~625 m) jamás podrían resolver. Los ríos no la
// usan: llevan su calado por vértice (la malla de agua va a resolución plena).
//
// Segunda textura (R8, un texel por quad del heightfield, filtro NEAREST):
// 1 = la malla de agua interior cubre ese quad y el mar se recorta en él. Es
// la MISMA cobertura que usa la malla (terrain/water_surface), alineada al
// quad: el mar nunca se descarta donde no hay lámina de río que lo sustituya.
struct SeabedTexture {
    GLuint id = 0;
    GLuint clip_id = 0;

    SeabedTexture() = default;
    SeabedTexture(const SeabedTexture&) = delete;
    SeabedTexture& operator=(const SeabedTexture&) = delete;
    ~SeabedTexture() { release(); }

    void release();
    void rebuild(const terrain::TerrainData& data);
};

// En heap y no-movible: ChunkedTerrain y DetailSampler guardan punteros a data.
// Recibe la TerrainData YA generada (el generate corre en un hilo de trabajo);
// aquí solo queda el trabajo con GL, que debe vivir en el hilo principal.
struct World {
    terrain::TerrainData data;
    terrain::DetailSampler sampler;
    terrain::ChunkedTerrain ground;
    std::optional<render::Mesh> water;
    render::Mesh sea;
    veg::VegRenderer vegetation;
    veg::GrassField grass;
    SeabedTexture seabed;

    explicit World(terrain::TerrainData&& ready);

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Altura de la cámara para una pose: alt metros sobre max(terreno, mar).
    float pose_height(const CameraPose& pose) const;
};

} // namespace app
