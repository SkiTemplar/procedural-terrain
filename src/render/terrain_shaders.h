#pragma once

namespace render {

// Flat shading sin duplicar vértices: la normal por cara sale de las derivadas
// de pantalla (dFdx/dFdy) y el color va `flat` (un color por triángulo).
constexpr const char* kTerrainVert = R"glsl(
#version 460 core
layout (location = 0) in vec3 a_pos;
layout (location = 1) in vec3 a_color;

uniform mat4 u_view;
uniform mat4 u_proj;

out vec3 v_world_pos;
flat out vec3 v_color;

void main() {
    v_world_pos = a_pos;
    v_color = a_color;
    gl_Position = u_proj * u_view * vec4(a_pos, 1.0);
}
)glsl";

constexpr const char* kTerrainFrag = R"glsl(
#version 460 core
in vec3 v_world_pos;
flat in vec3 v_color;

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

// Agua: olas suaves por vértice, semitransparente, con la misma niebla.
constexpr const char* kWaterVert = R"glsl(
#version 460 core
layout (location = 0) in vec3 a_pos;
layout (location = 1) in vec3 a_color;

uniform mat4 u_view;
uniform mat4 u_proj;
uniform float u_time;

out vec3 v_world_pos;
flat out vec3 v_color;

void main() {
    vec3 p = a_pos;
    p.y += 0.15 * sin(u_time * 1.1 + a_pos.x * 0.05 + a_pos.z * 0.07)
         + 0.08 * sin(u_time * 1.7 + a_pos.z * 0.13);
    v_world_pos = p;
    v_color = a_color;
    gl_Position = u_proj * u_view * vec4(p, 1.0);
}
)glsl";

constexpr const char* kWaterFrag = R"glsl(
#version 460 core
in vec3 v_world_pos;
flat in vec3 v_color;

uniform vec3 u_sun_dir;
uniform vec3 u_camera_pos;
uniform vec3 u_sky_color;
uniform float u_fog_density;

out vec4 frag_color;

void main() {
    vec3 normal = normalize(cross(dFdx(v_world_pos), dFdy(v_world_pos)));
    float diffuse = max(dot(normal, u_sun_dir), 0.0);
    vec3 lit = v_color * (0.45 + 0.55 * diffuse);

    // Un punto de especular del sol, muy barato.
    vec3 view_dir = normalize(u_camera_pos - v_world_pos);
    vec3 half_dir = normalize(view_dir + u_sun_dir);
    lit += vec3(0.35) * pow(max(dot(normal, half_dir), 0.0), 64.0);

    float dist = length(v_world_pos - u_camera_pos);
    float fog = 1.0 - exp(-u_fog_density * dist);
    vec3 color = mix(lit, u_sky_color, fog);

    frag_color = vec4(color, 0.78);
}
)glsl";

} // namespace render
