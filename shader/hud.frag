#version 450

layout(push_constant, std430) uniform Push {
  vec2  screen;
  float sharpen;
  } push;

layout(binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

// cubic B-spline filtering from 4 bilinear fetches: an enlarged all-or-nothing picture gets a smooth, rounded
// outline instead of the bilinear filter's straight segments between texel centres
vec4 bicubic(vec2 uv) {
  vec2 size = vec2(textureSize(tex,0));
  vec2 p    = uv*size-0.5;
  vec2 f    = fract(p);
  p        -= f;
  vec2 f2 = f*f, f3 = f2*f;
  vec2 w0 = (1.0-3.0*f+3.0*f2-f3)/6.0;
  vec2 w1 = (4.0-6.0*f2+3.0*f3)/6.0;
  vec2 w2 = (1.0+3.0*f+3.0*f2-3.0*f3)/6.0;
  vec2 w3 = f3/6.0;
  vec2 s0 = w0+w1, s1 = w2+w3;
  vec2 c0 = (p-0.5+w1/s0)/size, c1 = (p+1.5+w3/s1)/size;
  return (texture(tex,vec2(c0.x,c0.y))*s0.x+texture(tex,vec2(c1.x,c0.y))*s1.x)*s0.y
        +(texture(tex,vec2(c0.x,c1.y))*s0.x+texture(tex,vec2(c1.x,c1.y))*s1.x)*s1.y;
  }

void main() {
  if(push.sharpen<0.0) {
    // opaque: the picture's colour without its opacity (the menu's background, RCCars.exe 0x471610 mode 0)
    outColor = vec4(texture(tex, inUv).rgb*inColor.rgb, inColor.a);
    return;
    }
  if(push.sharpen>0.0) {
    // the font: its clear texels are black, so undo their darkening of the edge, then narrow the edge to about
    // a pixel around the smooth outline
    vec4 c = bicubic(inUv);
    c.rgb = clamp(c.rgb/max(c.a,1.0/255.0), 0.0, 1.0);
    c.a   = clamp((c.a-0.4)*push.sharpen+0.5, 0.0, 1.0);
    outColor = c*inColor;
    return;
    }
  outColor = texture(tex, inUv)*inColor;
  }
