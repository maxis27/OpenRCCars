#version 450
#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUvBase;
layout(location = 3) in vec2 inUvLight;
layout(location = 4) in vec2 inUvMask;
layout(location = 5) in vec2 inUvBase2;

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
  vec3 pos    = inPos;
  outNormal   = inNormal;
  outUvMask   = inUvMask;
  outUvBase   = inUvBase;
  if((push.flags & F_WATER)!=0u) {
    // the texture drifts ("texSpeedMin/Max" 13..28); water.tesc/.tese move the surface with the waves, level.frag
    // shades them
    outUvBase = inUvBase + push.time*vec2(0.013, 0.028);
    if((push.flags & (F_WAVES|F_TESS))==F_WAVES)
      pos.y += seaHeight(pos.xz, push.time, inUvMask.x);
    }
  outUvLight  = inUvLight;
  outUvBase2  = inUvBase2;
  outWorld    = pos;
  gl_Position = push.viewProj * vec4(pos, 1.0);
  }
