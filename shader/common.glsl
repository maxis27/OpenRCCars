#ifndef COMMON_GLSL
#define COMMON_GLSL

// keep in sync with LevelMesh::Push (game/world/levelmesh.h)
const uint F_LIGHTMAP = 1u;  // LM_1 layer present
const uint F_BLEND    = 2u;  // ALP1 + BAS2 terrain blending
const uint F_CUTOUT   = 4u;  // alpha test
const uint F_WATER    = 16u; // sea: inUvMask.x is the water depth, shallow water is see-through
const uint F_MASK     = 8u;  // ALP1 without BAS2: the mask is the base texture's opacity (shore foam)

layout(push_constant, std430) uniform Push {
  mat4  viewProj;
  vec4  cameraPos;    // xyz, w = fog distance
  vec4  sunDir;       // xyz, w = lightmap scale
  uint  flags;
  } push;

#endif
