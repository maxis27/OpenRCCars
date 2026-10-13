#version 450

layout(push_constant, std430) uniform Push {
  mat4 viewProj;
  uint mode;
  } push;

layout(binding = 0) uniform sampler2D tex;
layout(binding = 1) uniform sampler2D mask;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec2 inUv2;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
  if(push.mode==0u) {
    // tyre track (RCCars.exe 0x52fd00): the texture times the fade (alpha) plus the grey of the colour, which is
    // (1 - fade)/2, so 0.5 where the track fades out; blended as source x destination twice, which leaves the
    // ground as it is at 0.5
    vec3 t   = texture(tex, inUv).rgb;
    outColor = vec4(t*inColor.a + inColor.rgb, 1.0);
    return;
    }
  vec4 c = texture(tex, inUv)*inColor;
  c.a   *= texture(mask, inUv2).a;
  outColor = c;
  }
