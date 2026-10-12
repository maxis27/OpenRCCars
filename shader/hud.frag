#version 450

layout(push_constant, std430) uniform Push {
  vec2  screen;
  float sharpen;
  } push;

layout(binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
  vec4 c = texture(tex, inUv);
  if(push.sharpen>0.0) {
    // the font's clear texels are black: undo their darkening of the edge, then narrow the edge
    c.rgb = clamp(c.rgb/max(c.a,1.0/255.0), 0.0, 1.0);
    c.a   = clamp((c.a-0.5)*push.sharpen+0.5, 0.0, 1.0);
    }
  outColor = c*inColor;
  }
