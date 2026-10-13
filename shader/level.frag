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

  if((push.flags & F_DIRT)!=0u) {
    // the dust the car picked up (RCCars.exe 0x531c20, 0x507dc0): its dirt_car textures, painted like the car,
    // laid over it by their opacity; from the clean car to the first one, then from the first to the second
    float t  = push.time;
    vec4  hi = texture(texBase2, inUvBase);
    vec4  lo = t<1.0 ? vec4(hi.rgb, 0.0) : texture(texMask, inUvBase);
    vec4  d  = mix(lo, hi, clamp(t<1.0 ? t : t-1.0, 0.0, 1.0));
    color.rgb = mix(color.rgb, d.rgb, d.a);
    }

  if((push.flags & F_WAVES)!=0u) {
    // the sea after RCCars.exe 0x51c000 / 0x5240c0, see formats/water.h
    float edge = inUvMask.x;
    float s    = seaMagnet(edge);
    vec3  w    = seaWaves(inWorld.xz, push.time);
    // see-through towards the outline: 35/255 there, opaque from alphaRadius on
    float a = water.edge.y>0.0 ? clamp((35.0 + 220.0*edge/water.edge.y)/255.0, 35.0/255.0, 1.0) : 1.0;
    // but only near the car (distAlphaClampMin/Max), in a distance that weighs height 187 times
    vec3  d    = inWorld - push.carPos.xyz;
    float dc   = sqrt(dot(d.xz, d.xz) + 187.0*d.y*d.y);
    float near = 1.0 - clamp((dc - water.edge.z)/max(water.edge.w - water.edge.z, 1e-6), 0.0, 1.0);
    color.a    = mix(1.0, a, near);
    // grey from colorMin in the troughs to colorMax on the crests
    float lo = -(0.5*water.wave.x + water.wave.w), hi = water.wave.x + water.wave.w;
    color.rgb *= mix(water.color.x, water.color.y, clamp((s*w.x - lo)/max(hi - lo, 1e-6), 0.0, 1.0));
    normal     = normalize(vec3(-w.y*s, 1.0, -w.z*s));
    }
  else if((push.flags & F_WATER)!=0u) {
    // water without WLOD settings (pools, streams): WLOD_*.ini alphaMin 26/255 .. alphaMax 255, full opacity at
    // 0.8 m deep, nothing where the ground is above the water plane
    float depth = inUvMask.x;
    color.a = depth<=0.0 ? 0.0 : mix(26.0/255.0, 1.0, pow(clamp(depth/0.8, 0.0, 1.0), 0.6));
    normal  = vec3(0.0, 1.0, 0.0);
    }

  if((push.flags & F_MASK)!=0u) {
    // shore foam (RCCars.exe 0x51c690, formats/water.h CoastFoam): it follows wave 2 of the sea, showing as the
    // water draws back and fading as the next wave comes in; its texture scrolls by "ScrollVel"
    float fade = 1.0;
    vec2  uv   = inUvBase;
    if(water.color.z>0.0) {
      float p  = seaPhase2(inWorld.xz, push.time);
      float h  = sin(p), on = water.color.w;
      float lo = water.foam.x, hi = water.foam.y;
      if(cos(p)>0.0 && h>on)
        fade = lo;
      else if(cos(p)<0.0)
        fade = mix(hi, lo, (h+1.0)*0.5);
      else
        fade = h>on ? lo : mix(hi, lo, (h+1.0)/max(on+1.0, 1e-6));
      uv.y -= water.foam.z*push.time;
      }
    color.a   = texture(texMask, inUvMask).a * fade;
    color.rgb = texture(texBase, uv).rgb;
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
