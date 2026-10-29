#pragma once

// Constantes compartidas entre C++ y los shaders de terreno. Son macros (y no
// constexpr) porque los shaders son literales que se concatenan en tiempo de
// compilación: PT_GLSL_STR las convierte en texto GLSL, de modo que hay una
// sola definición para la malla y para el fragment shader.
// Sin dependencias de OpenGL: lo incluyen el mallado y los tests.

#define PT_GLSL_STR_IMPL(x) #x
#define PT_GLSL_STR(x) PT_GLSL_STR_IMPL(x)

// Domain warp del muestreo de material (terrain::material_warp en
// chunked_terrain.cpp y material_warp() en kTerrainFrag). Ambos usan el mismo
// ruido de valor (terrain::value_noise / value_noise en GLSL) con estas
// constantes, para que la textura de detalle no se desfase del color de paleta.
#define PT_MATERIAL_WARP_FREQ 0.05f   // 1/m: periodo del serpenteo ~20 m
#define PT_MATERIAL_WARP_AMP 5.0f     // m: desplazamiento máximo por eje
#define PT_MATERIAL_WARP_X_OFFSET_X 13.0f
#define PT_MATERIAL_WARP_X_OFFSET_Z -4.0f
#define PT_MATERIAL_WARP_Z_OFFSET_X -8.0f
#define PT_MATERIAL_WARP_Z_OFFSET_Z 21.0f

// Unidades de textura fijas de los samplers de detalle (layout(binding)).
// La unidad 0 la usa el fondo marino del shader de agua; la vegetación
// comparte el fragment del terreno, así que estas unidades deben tener
// siempre una textura válida del tipo correcto (ver TerrainTextures::bind).
#define PT_DETAIL_ALBEDO_UNIT 1
#define PT_DETAIL_NORMAL_UNIT 2
#define PT_MATERIAL_GRID_UNIT 3
#define PT_TERRAIN_NORMAL_MAP_UNIT 4
