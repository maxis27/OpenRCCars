#version 450

// marks lying in the world (game/effects.cpp): tyre tracks, the wake and the foam on the water

layout(push_constant, std430) uniform Push {
  mat4 viewProj;
  uint mode;      // 0 tyre track, 1 texture with a second one's opacity (wake, foam)
  } push;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec2 inUv2;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec2 outUv2;
layout(location = 2) out vec4 outColor;

out gl_PerVertex {
  vec4 gl_Position;
  };

void main() {
  outUv       = inUv;
  outUv2      = inUv2;
  outColor    = inColor;
  gl_Position = push.viewProj * vec4(inPos, 1.0);
  }
