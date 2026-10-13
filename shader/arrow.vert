#version 450

// the direction arrow (game/arrow.cpp): the cockpit.sb model through its own camera, unlit

layout(push_constant, std430) uniform Push {
  mat4 mvp;
  vec4 color;
  } push;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec2 outUv;

out gl_PerVertex {
  vec4 gl_Position;
  };

void main() {
  outUv       = inUv;
  gl_Position = push.mvp * vec4(inPos, 1.0);
  }
