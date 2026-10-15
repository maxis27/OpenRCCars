#version 450

layout(push_constant, std430) uniform Push {
  vec2  screen;
  uint  fogColor; // 0x00RRGGBB
  } push;

layout(binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2  inUv;
layout(location = 1) in vec4  inColor;
layout(location = 2) in float inFog;

layout(location = 0) out vec4 outColor;

void main() {
  vec4 c   = texture(tex, inUv)*inColor;
  // fogged like everything else (RCCars.exe draws its particles with the fog on)
  c.rgb    = mix(unpackUnorm4x8(push.fogColor).bgr, c.rgb, inFog);
  outColor = c;
  }
