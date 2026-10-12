#version 450

// 2D overlay (game/hud.cpp): positions in pixels, top left origin

layout(push_constant, std430) uniform Push {
  vec2 screen;  // framebuffer size in pixels
  } push;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;

out gl_PerVertex {
  vec4 gl_Position;
  };

void main() {
  outUv       = inUv;
  outColor    = inColor;
  gl_Position = vec4(inPos/push.screen*2.0-1.0, 0.0, 1.0);
  }
