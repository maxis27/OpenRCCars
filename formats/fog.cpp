#include "fog.h"

#include <cmath>
#include <string>

#include "pack.h"
#include "settings.h"

using namespace rc;

FogParams FogParams::parse(const SliderFile& f) {
  FogParams p;
  p.start   = float(f.raw("Start"));
  p.end     = float(f.raw("End"));
  p.density = float(f.raw("Density"))*0.001f;
  const auto ch = [&](std::string_view key) { return uint32_t(f.raw(key)) & 0xffu; };
  p.color   = 0xff000000u | ch("ColorR")<<16 | ch("ColorG")<<8 | ch("ColorB");
  return p;
  }

FogParams FogParams::load(PackFile& pack, std::string_view track) {
  const std::string file = "Settings\\fog_"+std::string(track)+".ini";
  if(pack.find(file)==nullptr) {
    FogParams p;
    p.end   = 1;
    p.color = 0xffffffffu;
    return p;
    }
  auto d = pack.read(file);
  return parse(SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size())));
  }

void Fog::setTarget(const FogParams& p) {
  from = now;
  to   = p;
  t    = 0;
  }

void Fog::tick(float dt) {
  t += dt;
  if(t>Fade) {
    now = to;
    return;
    }
  const float k    = 2.f*t;  // 1/Fade
  const auto  lerp = [k](float a, float b) { return a + (b-a)*k; };
  now.start   = lerp(from.start,  to.start);
  now.end     = lerp(from.end,    to.end);
  now.density = lerp(from.density,to.density);
  uint32_t c = 0;
  for(int sh=16; sh>=0; sh-=8) {
    const float v = lerp(float((from.color>>sh)&0xff),float((to.color>>sh)&0xff));
    c |= (uint32_t(int(v)) & 0xffu)<<sh;   // _ftol: truncated
    }
  now.color = c;
  }

float Fog::visibility(float density, float depth) {
  return std::exp(-density*depth);
  }
