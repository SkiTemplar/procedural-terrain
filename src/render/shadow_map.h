#pragma once

#include "render/shadow_cascades.h"

#include <glad/glad.h>

namespace render {

// Mapa de sombras en cascada del sol: textura 2D array DEPTH_COMPONENT32F
// (una capa por cascada) con comparación hardware (sampler2DArrayShadow →
// PCF bilineal por lectura) y borde 1.0 (fuera del mapa = al sol). RAII.
class ShadowMap {
public:
    explicit ShadowMap(int resolution);
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    // Liga el FBO con la capa `cascade` como depth, ajusta el viewport y
    // limpia la profundidad a 1. El llamador restaura su framebuffer.
    void begin_cascade(int cascade) const;

    void bind_texture(GLuint unit) const;
    int resolution() const { return resolution_; }

private:
    GLuint fbo_ = 0;
    GLuint depth_ = 0;
    int resolution_ = 0;
};

} // namespace render
