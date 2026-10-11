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
// wave code is not readable (encrypted exe), so wavelengths and speeds are picked by eye. The angles are read as
// where the waves come from (from +x towards +z): taken as the direction they travel, the waves of beach_1 ran out
// to sea, so they go the opposite way, onto the beach.
// Returns the height offset in .x and its gradient in .yz (d/dx, d/dz).
const float WAVE_LENGTH1 = 3.0, WAVE_LENGTH2 = 1.61;

// `amp` scales each of the two waves (water.tese leaves out the ones its mesh is too coarse for)
vec3 waterWaves(vec2 xz, float t, vec2 amp) {
  const float a1 = radians(240.0), a2 = radians(220.0);
  const vec2  d1 = vec2(cos(a1), sin(a1)), d2 = vec2(cos(a2), sin(a2));
  const float k1 = 6.2831853/WAVE_LENGTH1, w1 = k1*0.6, h1 = 0.08*amp.x;
  const float k2 = 6.2831853/WAVE_LENGTH2, w2 = k2*0.5, h2 = 0.125*amp.y;
  float p1 = k1*dot(d1,xz) - w1*t;
  float p2 = k2*dot(d2,xz) - w2*t;
  float y  = h1*sin(p1) + h2*sin(p2);
  vec2  g  = h1*k1*cos(p1)*d1 + h2*k2*cos(p2)*d2;
  return vec3(y, g);
  }

vec3 waterWaves(vec2 xz, float t) {
  return waterWaves(xz, t, vec2(1.0));
  }

// the waves die out towards the shore: full height from 0.3 m deep
float waterShallow(float depth) {
  return clamp(depth/0.3, 0.0, 1.0);
  }

// how far apart the vertices of the moving sea are (water.tesc) at `dist` metres from the camera
float waterSpacing(float dist) {
  return max(0.15, dist*0.02);
  }

// which waves a mesh with vertices `spacing` metres apart can show without flickering: 8 or more per wave
vec2 waterDetail(float spacing) {
  return vec2(1.0) - smoothstep(vec2(WAVE_LENGTH1, WAVE_LENGTH2)/8.0, vec2(WAVE_LENGTH1, WAVE_LENGTH2)/4.0, vec2(spacing));
  }

#endif
