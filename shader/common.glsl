#ifndef COMMON_GLSL
#define COMMON_GLSL

// keep in sync with LevelMesh::Push (game/world/levelmesh.h)
const uint F_LIGHTMAP = 1u;  // LM_1 layer present
const uint F_BLEND    = 2u;  // ALP1 + BAS2 terrain blending
const uint F_CUTOUT   = 4u;  // alpha test
const uint F_WATER    = 16u; // sea: inUvMask.x is the water depth, shallow water is see-through
const uint F_MASK     = 8u;  // ALP1 without BAS2: the mask is the base texture's opacity (shore foam)
const uint F_WAVES    = 32u; // sea with WLOD waves (Water block): inUvMask.x is the distance to its outline
const uint F_TESS     = 64u; // drawn with water.tesc/.tese, which move the vertices instead of level.vert

layout(push_constant, std430) uniform Push {
  mat4  viewProj;
  vec4  cameraPos;    // xyz, w = fog distance
  vec4  sunDir;       // xyz, w = lightmap scale
  uint  flags;
  float time;         // seconds
  vec4  carPos;       // the player's car
  } push;

// keep in sync with WaterBlock (game/world/levelmesh.h); formats/water.h has the formulas
layout(binding = 4, std140) uniform Water {
  vec4 wave;          // amp, period, speed, amp2
  vec4 wave2;         // period2, 1/length2, center x, center z
  vec4 dir;           // cos(angle), sin(angle), offset, magnetOffset
  vec4 edge;          // 1/magnetRadius, alphaRadius, distAlphaClampMin, distAlphaClampMax
  vec4 color;         // colorMin, colorMax (0..1), 1 = set, foam heightOn
  vec4 foam;          // foam alphaMin, alphaMax, scrollVel
  } water;

// phase of wave 2, which runs in to its center; the shore foam follows it
float seaPhase2(vec2 xz, float t) {
  return distance(xz, water.wave2.zw)*water.wave2.y + water.wave2.x*t;
  }

// the two waves of the sea (RCCars.exe 0x5240c0): height in .x, its gradient in .yz
vec3 seaWaves(vec2 xz, float t) {
  float p1 = water.wave.y*(dot(xz, water.dir.xy) + water.wave.z*t);
  float p2 = seaPhase2(xz, t);
  vec2  r  = xz - water.wave2.zw;
  float y  = water.wave.x*(0.75*sin(p1) + 0.25) + water.wave.w*sin(p2);
  vec2  g  = water.wave.x*0.75*cos(p1)*water.wave.y*water.dir.xy +
             water.wave.w*cos(p2)*water.wave2.y*r/max(length(r), 1e-3);
  return vec3(y, g);
  }

// how much of the waves a vertex `edge` metres from the outline of the sea gets
float seaMagnet(float edge) {
  return water.edge.x>0.0 ? clamp(edge*water.edge.x, 0.0, 1.0) : 1.0;
  }

// height of the sea above the scene's surface: lowered by "offset", pulled by "magnetOffset" near the outline
float seaHeight(vec2 xz, float t, float edge) {
  float s = seaMagnet(edge);
  float y = water.dir.z + (1.0-s)*water.dir.w + s*seaWaves(xz, t).x;
  return edge<0.01 ? y + water.dir.w : y;
  }

// how far apart the vertices of the moving sea are (water.tesc) at `dist` metres from the camera
float waterSpacing(float dist) {
  return max(0.15, dist*0.02);
  }

#endif
