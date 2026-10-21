#include "aiemu.h"

#include "formats/settings.h"

using namespace rc;

AiEmu::AiEmu() = default;

void AiEmu::load(const SliderFile& ini) {
  for(size_t i=0; i<chance.size(); ++i)
    chance[i] = float(ini.raw("ProbCheck"+std::to_string(i+1),int32_t(chance[i])));
  }

void AiEmu::restart() {
  timers.clear();
  last.clear();
  }

std::vector<AiEmu::Encounter> AiEmu::update(float dt, float now, const std::vector<Spot>& spots,
                                            const std::vector<Car>& cars) {
  std::vector<Encounter> out;
  if(last.size()<cars.size())
    last.resize(cars.size(),{0,0,0});
  std::uniform_real_distribution<float> percent(0.f,100.f);
  for(auto& s:spots) {
    const size_t k = size_t(s.kind);
    // 0x4a1240: counts down, and checks the cars when it runs out (the first time at once)
    float& t = timers[{s.kind,s.id}];
    t -= dt;
    if(t>=0)
      continue;
    t = Period[k];
    if(!s.there)
      continue;
    for(size_t i=0; i<cars.size(); ++i) {
      if(!cars[i].racing || now-last[i][k]<Cooldown[k] || (cars[i].pos-s.pos).length()>=Reach)
        continue;
      if(percent(rng)>=chance[i%chance.size()])
        continue;
      last[i][k] = now;
      out.push_back({i,s.kind});
      }
    }
  return out;
  }
