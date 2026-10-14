#version 450

layout(push_constant, std430) uniform Push {
  mat4 viewProj;
  uint mode;
  uint  fogColor;   // 0x00RRGGBB
  float fogDensity; // formats/fog.h
  } push;

layout(binding = 0) uniform sampler2D tex;
layout(binding = 1) uniform sampler2D mask;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec2 inUv2;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec4 outColor;

// how much of the colour the fog leaves: exp(-density*w)
float fogKeep() {
  return exp(-push.fogDensity/gl_FragCoord.w);
  }

void main() {
  if(push.mode==0u) {
    // tyre track (RCCars.exe 0x52fd00): the texture times the fade (alpha) plus the grey of the colour, which is
    // (1 - fade)/2, so 0.5 where the track fades out; blended as source x destination twice, which leaves the
    // ground as it is at 0.5
    vec3 t   = texture(tex, inUv).rgb;
    outColor = vec4(t*inColor.a + inColor.rgb, 1.0);
    // fogged towards half the fog's colour, as RCCars.exe does for its x2 blends (0x463e70): what it fogs is
    // doubled by the blend
    outColor.rgb = mix(unpackUnorm4x8(push.fogColor).bgr*0.5, outColor.rgb, fogKeep());
    return;
    }
  vec4 c = texture(tex, inUv)*inColor;
  c.a   *= texture(mask, inUv2).a;
  c.rgb    = mix(unpackUnorm4x8(push.fogColor).bgr, c.rgb, fogKeep());
  outColor = c;
  }
