#version 450

// the cars' particles (game/effects.cpp): hud.vert's sprites in pixels with a depth, and how much of their colour
// the fog leaves at that depth

layout(push_constant, std430) uniform Push {
  vec2  screen;   // framebuffer size in pixels
  uint  fogColor; // 0x00RRGGBB
  } push;

layout(location = 0) in vec2  inPos;
layout(location = 1) in vec2  inUv;
layout(location = 2) in vec4  inColor;
layout(location = 3) in float inDepth;  // depth 0..1, tested against the scene
layout(location = 4) in float inFog;    // exp(-density*w) at the sprite (formats/fog.h)

layout(location = 0) out vec2  outUv;
layout(location = 1) out vec4  outColor;
layout(location = 2) out float outFog;

out gl_PerVertex {
  vec4 gl_Position;
  };

void main() {
  outUv       = inUv;
  outColor    = inColor;
  outFog      = inFog;
  gl_Position = vec4(inPos/push.screen*2.0-1.0, inDepth, 1.0);
  }
