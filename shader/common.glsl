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
  float time;         // seconds
  } push;

// Waves of the sea, after Settings/WLOD_*.ini (beach_1 more1): two directional waves, "angle" 6 and "angle2" 4
// in 10 degree steps, "amp" 16 and "amp2" 25 read as half-centimetres, "length2" 161 as centimetres. The original's
// wave code is not readable (encrypted exe), so wavelengths and speeds are picked by eye.
// Returns the height offset in .x and its gradient in .yz (d/dx, d/dz).
vec3 waterWaves(vec2 xz, float t) {
  const float a1 = radians(60.0), a2 = radians(40.0);
  const vec2  d1 = vec2(cos(a1), sin(a1)), d2 = vec2(cos(a2), sin(a2));
  const float k1 = 6.2831853/3.0,  w1 = k1*0.6, h1 = 0.08;
  const float k2 = 6.2831853/1.61, w2 = k2*0.5, h2 = 0.125;
  float p1 = k1*dot(d1,xz) - w1*t;
  float p2 = k2*dot(d2,xz) - w2*t;
  float y  = h1*sin(p1) + h2*sin(p2);
  vec2  g  = h1*k1*cos(p1)*d1 + h2*k2*cos(p2)*d2;
  return vec3(y, g);
  }

#endif
