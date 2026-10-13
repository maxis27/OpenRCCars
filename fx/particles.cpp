#include "particles.h"

#include <algorithm>
#include <cmath>

using namespace rc;
using namespace rc::fx;

void Emitter::moveTo(const Vec3& p) {
  at = p;
  if(!placed) {
    last   = p;
    placed = true;
    }
  }

size_t Emitter::count(float rate, float dt) {
  // RCCars.exe 0x4776e0: the whole part of rate*dt now, the rest added up until it makes one more
  if(!(rate>0) || dt<=0)
    return 0;
  const float n     = rate*dt;
  float       whole = std::floor(n);
  carry += n-whole;
  if(carry>1.f) {
    whole += std::floor(carry);
    carry -= std::floor(carry);
    }
  return size_t(whole);
  }

Vec3 Emitter::wayPoint(size_t i, size_t n) const {
  const float k = n>0 ? float(i)/float(n) : 0.f;
  return last + (at-last)*k;
  }

Particle* Emitter::spawn(const Vec3& p) {
  if(live.size()>=capacity)
    return nullptr;
  live.emplace_back();
  live.back().pos  = p;
  live.back().base = p;
  return &live.back();
  }

void Emitter::age(float dt) {
  for(auto& p:live)
    p.left -= dt;
  live.erase(std::remove_if(live.begin(),live.end(),[](const Particle& p){ return p.left<0; }),live.end());
  }
