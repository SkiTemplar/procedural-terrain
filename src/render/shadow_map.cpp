#include "render/shadow_map.h"

#include <stdexcept>

namespace render {

ShadowMap::ShadowMap(int resolution) : resolution_(resolution) {
    glGenTextures(1, &depth_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, depth_);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH_COMPONENT32F, resolution, resolution,
                   kShadowCascadeCount);
    // LINEAR + comparación: cada lectura devuelve el PCF bilineal de 2x2
    // texeles, gratis en el hardware.
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depth_, 0, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo_);
        glDeleteTextures(1, &depth_);
        throw std::runtime_error("framebuffer del mapa de sombras incompleto");
    }
}

ShadowMap::~ShadowMap() {
    glDeleteFramebuffers(1, &fbo_);
    glDeleteTextures(1, &depth_);
}

void ShadowMap::begin_cascade(int cascade) const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depth_, 0, cascade);
    glViewport(0, 0, resolution_, resolution_);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void ShadowMap::bind_texture(GLuint unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, depth_);
    glActiveTexture(GL_TEXTURE0);
}

} // namespace render
