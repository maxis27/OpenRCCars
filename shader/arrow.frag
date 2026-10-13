#version 450

layout(push_constant, std430) uniform Push {
  mat4 mvp;
  vec4 color;
  } push;

layout(binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main() {
  // coc_str_2 is see-through around the chevron and under the stripe the sides use: the model is drawn opaque
  outColor = vec4(texture(tex, inUv).rgb * push.color.rgb, 1.0);
  }
