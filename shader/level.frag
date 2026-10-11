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
  vec4 color  = texture(texBase, inUvBase);
  vec3 normal = normalize(inNormal);

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
    // waves (dying out towards the shore) light the surface, white caps on the crests ("colorMin" 175 ..
    // "colorMax" 255)
    vec3  w     = waterWaves(inWorld.xz, push.time) * clamp(depth/0.3, 0.0, 1.0);
    float crest = smoothstep(0.45, 0.95, w.x/0.205);
    color.rgb = mix(color.rgb, vec3(0.95), crest*0.6);
    color.a   = max(color.a, crest*0.8*step(0.0, depth));
    normal    = normalize(vec3(-w.y, 1.0, -w.z));
    }

  if((push.flags & F_MASK)!=0u) {
    // the foam washes up and down the beach (waterLOD_coast.ini: "HeightOn" 100, "HeightOff" 68 read as how
    // much of the strip it covers, "AlphaMin" 36 .. "AlphaMax" 255, "ScrollVel" 11). The mask's V runs from
    // the top of the strip (0, the wavy edge) down into the sea (1). A wave every 4 s, out of step along the shore.
    float phase = push.time*6.2831853/4.0 - dot(inWorld.xz, vec2(0.35, 0.21));
    float s     = 0.5 + 0.5*sin(phase);                // 1 = washed up
    float shift = (1.0-s)*0.32;
    vec2  uv    = inUvMask + vec2(0.0, -shift);
    color.a     = uv.y<0.0 ? 0.0 : texture(texMask, uv).a;
    color.a    *= mix(36.0/255.0, 1.0, s);
    color.rgb   = texture(texBase, inUvBase + vec2(0.0, -push.time*0.11*0.1)).rgb;
    }

  if((push.flags & F_CUTOUT)!=0u && color.a<0.5)
    discard;

  vec3 light;
  if((push.flags & F_LIGHTMAP)!=0u) {
    light = texture(texLight, inUvLight).rgb * push.sunDir.w;
    } else {
    float d = max(dot(normal, normalize(push.sunDir.xyz)), 0.0);
    light   = vec3(0.55) + vec3(0.55)*d;
    }
  if((push.flags & F_WATER)!=0u) {
    // sun glitter on the waves
    vec3  v = normalize(push.cameraPos.xyz - inWorld);
    vec3  h = normalize(v + normalize(push.sunDir.xyz));
    light  += vec3(0.6)*pow(max(dot(normal, h), 0.0), 60.0);
    }
  color.rgb *= light;

  // simple distance fog, the original game uses fog too (VIDEO_UseFog)
  float dist = length(inWorld - push.cameraPos.xyz);
  float fog  = clamp(dist/push.cameraPos.w, 0.0, 1.0);
  color.rgb  = mix(color.rgb, vec3(0.62,0.72,0.82), fog*fog*0.6);

  outColor = color;
  }
