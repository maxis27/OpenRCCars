#version 450
#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"

layout(location = 0) in vec3 inBary;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
  // triangle edges about 1.5 px wide, independent of distance
  vec3  w    = fwidth(inBary);
  vec3  e    = smoothstep(vec3(0.0), w*1.5, inBary);
  float edge = 1.0 - min(min(e.x, e.y), e.z);
  outColor   = vec4(mix(inColor.rgb, inColor.rgb*0.35, edge), mix(inColor.a, 0.9, edge));
  }
