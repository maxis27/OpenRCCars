#include "wake.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace rc;
using namespace rc::fx;

namespace {

// 0x5738c8, as in the tyre tracks
constexpr float TyreWidth[4] = {0.95f, 1.07f, 1.25f, 1.35f};

// the corners' second texture coordinates: the opacity mask spans the quad once
constexpr float MaskUv[4][2] = {{0,0},{1,0},{1,1},{0,1}};

// 0x529940: bounces off min and max
void bounce(float& v, int& dir, float lo, float hi) {
  if(v>hi) {
    v   = hi-(v-hi);
    dir = -1;
    }
  else if(v<lo) {
    v   = (lo-v)+lo;
    dir = 1;
    }
  }

}

Wake::Wake(const WakeSettings& wake, const FoamSettings& foam)
  : wakeSet(wake), foamSet(foam) {
  }

void Wake::clear() {
  for(auto& t:traces)
    t.inWater = false;
  for(auto& h:horses)
    h.inWater = false;
  }

void Wake::update(const Car& car, float dt, Random& rnd) {
  // 0x528da0: the rear wheels and the body
  traces.resize(3);
  if(car.wheels.size()>=4) {
    trace(traces[0],car.wheels[2],car,dt,rnd);
    trace(traces[1],car.wheels[3],car,dt,rnd);
    }
  // the wheel numbered as many as the car has wheels: the body (0x531df0)
  trace(traces[2],car.body,car,dt,rnd);

  // 0x526140: the wheels (4, or 6), then the body a little deeper
  horses.resize(car.wheels.size()+1);
  for(size_t i=0; i<car.wheels.size(); ++i)
    horse(horses[i],car.wheels[i],car,false,dt);
  horse(horses.back(),car.bodyDeep,car,true,dt);
  }

void Wake::trace(Trace& t, const Probe& p, const Car& car, float dt, Random& rnd) const {
  // 0x528ed0
  const auto& s   = wakeSet;
  const bool  was = t.inWater;
  t.inWater = p.inWater && std::abs(p.depth)<p.limit;
  if(!t.inWater)
    return;
  const float speed = car.velocity.length()*3.6f;
  if(speed<1.f) {
    t.inWater = false;
    return;
    }
  // where the wheel goes (the original takes the body's speed at that point; ours leaves its turning out)
  t.forward = Vec3::dot(car.velocity,car.forward)>-1e-6f;
  if(!was) {
    t.scroll = 0;
    t.dir[0] = 1;
    t.dir[1] = -1;
    for(float& l:t.len)
      l = (s.animMax+s.animMin)*0.5f+(s.animMax-s.animMin)*0.5f*rnd.sym();
    }

  // longer with speed up to 30 km/h
  const float scale = speed<=30.f ? speed*0.025f+0.25f : 1.f;
  // fainter as the wheel comes out of the water or sinks into it
  const float sub = -p.depth, a = std::abs(sub), r = p.limit, k = 0.75f*r;
  float fade = 1;
  if(a>=k) {
    if(a>r)
      fade = sub<0 ? 0.1f : 0.4f;
    else if(std::abs(r-k)>=1e-6f)
      fade = 1.f-(a-k)*(sub<0 ? 0.9f : 0.6f)/(r-k);
    else
      fade = 0;
    }

  float za = s.forwZ, xa = s.forwX, zb = s.backZ, xb = s.backX;
  if(!t.forward) {
    std::swap(za,zb);
    std::swap(xa,xb);
    }
  za *= scale;
  zb *= scale;
  const Vec3& z = car.forward;
  const Vec3& x = car.right;
  Vec3 c[4] = {z*za+x*xa, z*za-x*xa, z*(-zb)-x*xb, z*(-zb)+x*xb};
  for(auto& v:c) {
    v   = v+p.pos;
    v.y = p.waterY;
    v   = v+p.normal*0.01f;
    }
  if(!t.forward) {
    std::swap(c[0],c[2]);
    std::swap(c[1],c[3]);
    }
  // the texture runs along the wake, squeezed by `len` at the front and the back corners
  const float half[4] = {-scale*t.len[0]*0.5f, -scale*t.len[1]*0.5f, scale*t.len[0]*0.5f, scale*t.len[1]*0.5f};
  const float u[4]    = {0,1,1,0};
  const float alpha[4] = {s.forwA,s.forwA,s.backA,s.backA};
  t.quad.kind = Mark::Wake;
  for(int i=0; i<4; ++i) {
    auto& v = t.quad.v[i];
    v.pos     = c[i];
    v.uv[0]   = u[i];
    v.uv[1]   = t.scroll+half[i];
    v.uv2[0]  = MaskUv[i][0];
    v.uv2[1]  = MaskUv[i][1];
    v.rgba[0] = v.rgba[1] = v.rgba[2] = 1;
    v.rgba[3] = std::floor(alpha[i]*fade)/255.f;
    }

  // the texture runs by wtr_text.spl (% a second), and its stretch breathes between AnimDUVMin and Max
  const float run = s.speed.empty() ? 0.f : s.speed.at(speed)*0.01f;
  if(t.forward) {
    t.scroll -= run*dt;
    if(t.scroll<1.f)
      t.scroll += 100.f;
    }
  else {
    t.scroll += run*dt;
    if(t.scroll>100.f)
      t.scroll -= 100.f;
    }
  t.len[0] += (t.dir[0]>=0 ? 1.f : -1.f)*s.animSpeed*dt*0.75f;
  t.len[1] += (t.dir[1]>=0 ? 1.f : -1.f)*s.animSpeed*dt*1.25f;
  bounce(t.len[0],t.dir[0],s.animMin,s.animMax);
  bounce(t.len[1],t.dir[1],s.animMin,s.animMax);
  }

void Wake::horse(Horse& h, const Probe& p, const Car& car, bool body, float dt) const {
  // 0x5262e0
  const auto& s   = foamSet;
  const bool  was = h.inWater;
  const float a   = std::abs(p.depth), r = p.limit;
  h.inWater = p.inWater && a<r;
  if(!h.inWater)
    return;
  if(!was) {
    h.scroll = 0;
    h.timer  = 0;
    }

  Vec3  along, up;
  float halfZ, halfX, fade;
  if(body) {
    along = car.forward;
    up    = car.up;
    halfZ = s.forwZBody;
    halfX = s.forwXBody;
    fade  = a>r ? 0.f : (std::abs(r)<1e-6f ? 0.f : 1.f-a/r);
    }
  else {
    // as long as the chord the water line cuts from the wheel, as wide as the tyre
    const float chord = a>r ? 0.f : std::sqrt(std::max(1.f-(a/r)*(a/r),0.f));
    along = p.heading;
    up    = Vec3::cross(p.heading,p.axle);
    halfZ = s.forwZ*chord;
    halfX = TyreWidth[std::clamp(car.tyre,0,3)]*s.forwX;
    const float k = 0.3f*r;
    if(a<k)
      fade = 1;
    else if(a>r)
      fade = 0;
    else
      fade = std::abs(r-k)<1e-6f ? 0.f : 1.f-(a-k)/(r-k);
    }
  // lying on the water, under the wheel (or the body) along its up axis
  Vec3 side = Vec3::cross(along,p.normal);
  if(side.length()>=1e-6f)
    side = side.normalized();
  if(std::abs(up.y)<1e-6f) {
    h.inWater = false;    // on its side: the original puts the foam at the world's origin then
    return;
    }
  const Vec3 at = p.pos+up*((p.waterY-p.pos.y)/up.y);
  if((at-p.pos).length()>r) {
    h.inWater = false;
    return;
    }
  const Vec3 c[4] = {side*halfX+along*halfZ, side*(-halfX)+along*halfZ, side*(-halfX)-along*halfZ, side*halfX-along*halfZ};
  const float tiles = body ? 4.f : 1.f;
  const float u[4]  = {0,tiles,tiles,0};
  const float v[4]  = {0,0,tiles,tiles};
  h.quad.kind = Mark::Foam;
  for(int i=0; i<4; ++i) {
    auto& q = h.quad.v[i];
    q.pos     = c[i]+at+p.normal*s.normalOffset;
    q.uv[0]   = u[i];
    q.uv[1]   = h.scroll+v[i];
    q.uv2[0]  = MaskUv[i][0];
    q.uv2[1]  = MaskUv[i][1];
    q.rgba[0] = q.rgba[1] = q.rgba[2] = 1;
    q.rgba[3] = std::floor(s.forwA*fade)/255.f;
    }

  // the texture jumps on every AnimDelay seconds
  h.timer += dt;
  if(h.timer>s.animDelay) {
    h.timer   = 0;
    h.scroll -= dt*5.f;
    if(h.scroll<0)
      h.scroll += 100.f;
    }
  }

void Wake::collect(std::vector<Mark>& out) const {
  // 0x528e30 draws the body's first, then the rear wheels'; 0x5261e0 the wheels' foam, then the body's
  for(size_t i: {size_t(2),size_t(0),size_t(1)})
    if(i<traces.size() && traces[i].inWater)
      out.push_back(traces[i].quad);
  for(auto& h:horses)
    if(h.inWater)
      out.push_back(h.quad);
  }
