#ifndef COMMON_GLSL
#define COMMON_GLSL

// keep in sync with LevelMesh::Push (game/world/levelmesh.h)
const uint F_LIGHTMAP = 1u;  // LM_1 layer present
const uint F_BLEND    = 2u;  // ALP1 + BAS2 terrain blending
const uint F_CUTOUT   = 4u;  // alpha test

layout(push_constant, std430) uniform Push {
  mat4  viewProj;
  vec4  cameraPos;    // xyz, w = fog distance
  vec4  sunDir;       // xyz, w = lightmap scale
  uint  flags;
  } push;

#endif
