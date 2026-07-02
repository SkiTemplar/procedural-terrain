#pragma once

namespace render {

// Flat shading sin duplicar vértices: la normal por cara sale de las derivadas
// de pantalla (dFdx/dFdy) de la posición en mundo.
constexpr const char* kTerrainVert = R"glsl(
#version 460 core
layout (location = 0) in vec3 a_pos;
layout (location = 1) in vec3 a_color;

uniform mat4 u_view;
uniform mat4 u_proj;

out vec3 v_world_pos;
out vec3 v_color;

void main() {
    v_world_pos = a_pos;
    v_color = a_color;
    gl_Position = u_proj * u_view * vec4(a_pos, 1.0);
}
)glsl";

constexpr const char* kTerrainFrag = R"glsl(
#version 460 core
in vec3 v_world_pos;
in vec3 v_color;

uniform vec3 u_sun_dir;      // hacia el sol, normalizado
uniform vec3 u_camera_pos;
uniform vec3 u_sky_color;
uniform float u_fog_density; // exponencial

out vec4 frag_color;

void main() {
    vec3 normal = normalize(cross(dFdx(v_world_pos), dFdy(v_world_pos)));
    float diffuse = max(dot(normal, u_sun_dir), 0.0);
    vec3 lit = v_color * (0.35 + 0.65 * diffuse);

    float dist = length(v_world_pos - u_camera_pos);
    float fog = 1.0 - exp(-u_fog_density * dist);
    vec3 color = mix(lit, u_sky_color, fog);

    frag_color = vec4(color, 1.0);
}
)glsl";

} // namespace render
