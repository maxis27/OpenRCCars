#version 450
#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"

// Moves the split sea (water.tesc) up and down with the waves (seaHeight).
layout(triangles, equal_spacing, ccw) in;

layout(location = 0) in vec3 inNormal[];
layout(location = 1) in vec2 inUvBase[];
layout(location = 2) in vec2 inUvLight[];
layout(location = 3) in vec2 inUvMask[];
layout(location = 4) in vec2 inUvBase2[];
layout(location = 5) in vec3 inWorld[];

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUvBase;
layout(location = 2) out vec2 outUvLight;
layout(location = 3) out vec2 outUvMask;
layout(location = 4) out vec2 outUvBase2;
layout(location = 5) out vec3 outWorld;

out gl_PerVertex {
  vec4 gl_Position;
  };

void main() {
  vec3 b = gl_TessCoord;
  outNormal  = b.x*inNormal [0] + b.y*inNormal [1] + b.z*inNormal [2];
  outUvBase  = b.x*inUvBase [0] + b.y*inUvBase [1] + b.z*inUvBase [2];
  outUvLight = b.x*inUvLight[0] + b.y*inUvLight[1] + b.z*inUvLight[2];
  outUvMask  = b.x*inUvMask [0] + b.y*inUvMask [1] + b.z*inUvMask [2];
  outUvBase2 = b.x*inUvBase2[0] + b.y*inUvBase2[1] + b.z*inUvBase2[2];
  vec3 pos   = b.x*inWorld  [0] + b.y*inWorld  [1] + b.z*inWorld  [2];

  if((push.flags & F_WAVES)!=0u)
    pos.y += seaHeight(pos.xz, push.time, outUvMask.x);

  outWorld    = pos;
  gl_Position = push.viewProj * vec4(pos, 1.0);
  }
