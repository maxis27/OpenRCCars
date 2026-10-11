#version 450
#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inBary;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec3 outBary;
layout(location = 1) out vec4 outColor;

out gl_PerVertex {
  vec4 gl_Position;
  };

void main() {
  outBary     = inBary;
  outColor    = inColor;
  gl_Position = push.viewProj * vec4(inPos, 1.0);
  }
