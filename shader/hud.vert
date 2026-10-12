#version 450

// 2D overlay (game/hud.cpp): positions in pixels, top left origin; a depth for sprites placed in the 3D view

layout(push_constant, std430) uniform Push {
  vec2 screen;  // framebuffer size in pixels
  } push;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;
layout(location = 3) in float inDepth;  // depth 0..1 for sprites in the 3D view, tested against the scene

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;

out gl_PerVertex {
  vec4 gl_Position;
  };

void main() {
  outUv       = inUv;
  outColor    = inColor;
  gl_Position = vec4(inPos/push.screen*2.0-1.0, inDepth, 1.0);
  }
