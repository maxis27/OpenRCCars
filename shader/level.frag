#version 450
#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"

layout(binding = 0) uniform sampler2D texBase;
layout(binding = 1) uniform sampler2D texLight;
layout(binding = 2) uniform sampler2D texMask;
layout(binding = 3) uniform sampler2D texBase2;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUvBase;
layout(location = 2) in vec2 inUvLight;
layout(location = 3) in vec2 inUvMask;
layout(location = 4) in vec2 inUvBase2;
layout(location = 5) in vec3 inWorld;

layout(location = 0) out vec4 outColor;

void main() {
  vec4 color = texture(texBase, inUvBase);

  if((push.flags & F_BLEND)!=0u) {
    // ALP1 mask selects between the two terrain textures
    float m  = texture(texMask, inUvMask).a;
    vec4  c2 = texture(texBase2, inUvBase2);
    color.rgb = mix(c2.rgb, color.rgb, m);
    }

  if((push.flags & F_WATER)!=0u) {
    // WLOD_*.ini alphaMin 26/255 .. alphaMax 255; full opacity at 0.8 m deep (distAlphaClampMax 8, a guess
    // at its unit), nothing where the ground is above the sea plane
    float depth = inUvMask.x;
    color.a = depth<=0.0 ? 0.0 : mix(26.0/255.0, 1.0, pow(clamp(depth/0.8, 0.0, 1.0), 0.6));
    }

  if((push.flags & F_MASK)!=0u)
    color.a = texture(texMask, inUvMask).a;

  if((push.flags & F_CUTOUT)!=0u && color.a<0.5)
    discard;

  vec3 light;
  if((push.flags & F_LIGHTMAP)!=0u) {
    light = texture(texLight, inUvLight).rgb * push.sunDir.w;
    } else {
    vec3  n = normalize(inNormal);
    float d = max(dot(n, normalize(push.sunDir.xyz)), 0.0);
    light   = vec3(0.55) + vec3(0.55)*d;
    }
  color.rgb *= light;

  // simple distance fog, the original game uses fog too (VIDEO_UseFog)
  float dist = length(inWorld - push.cameraPos.xyz);
  float fog  = clamp(dist/push.cameraPos.w, 0.0, 1.0);
  color.rgb  = mix(color.rgb, vec3(0.62,0.72,0.82), fog*fog*0.6);

  outColor = color;
  }
