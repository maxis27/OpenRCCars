#include "carstate.h"

#include "bytes.h"

#include <algorithm>
#include <cmath>

namespace rc::net {

namespace {

int16_t fixed16(float v, float scale) {
  return int16_t(std::clamp(std::lround(v*scale),-32767l,32767l));
  }

void normalize(float q[4]) {
  float len = 0;
  for(int k=0; k<4; ++k)
    len += q[k]*q[k];
  len = len>0 ? 1.f/std::sqrt(len) : 1.f;
  for(int k=0; k<4; ++k)
    q[k] *= len;
  }

// the rotation `q` (w x y z) turned on by `dt` s at the angular velocity `spin` (world): dq/dt = 1/2 (0, spin) q
void turned(const float q[4], const Vec3& spin, float dt, float out[4]) {
  const float w = q[0], x = q[1], y = q[2], z = q[3];
  const Vec3  o = spin*(0.5f*dt);
  out[0] = w+(-o.x*x-o.y*y-o.z*z);
  out[1] = x+( o.x*w+o.y*z-o.z*y);
  out[2] = y+( o.y*w+o.z*x-o.x*z);
  out[3] = z+( o.z*w+o.x*y-o.y*x);
  normalize(out);
  }

CarProfile::Frame lerp(const CarProfile::Frame& a, const CarProfile::Frame& b, float t) {
  CarProfile::Frame f = a;
  const float span = b.time-a.time;
  f.time     = a.time+span*t;
  // along a curve that leaves a and reaches b with their velocities (cubic Hermite): at a few states a second a
  // straight line would cut the corners
  const float t2 = t*t, t3 = t2*t;
  f.position = a.position*(2*t3-3*t2+1) + a.velocity*((t3-2*t2+t)*span) + b.position*(-2*t3+3*t2)
             + b.velocity*((t3-t2)*span);
  f.velocity = a.velocity+(b.velocity-a.velocity)*t;
  f.spin     = a.spin+(b.spin-a.spin)*t;
  // the rotation: a's turned on and b's turned back with their angular velocities, then blended (normalised linear,
  // along the shorter way, as CarProfile::pose)
  float qa[4], qb[4];
  turned(a.rot,a.spin,span*t,qa);
  turned(b.rot,b.spin,-span*(1-t),qb);
  float d = 0;
  for(int k=0; k<4; ++k)
    d += qa[k]*qb[k];
  const float s = d<0 ? -1.f : 1.f;
  for(int k=0; k<4; ++k)
    f.rot[k] = qa[k]+(qb[k]*s-qa[k])*t;
  normalize(f.rot);
  for(int i=0; i<6; ++i)
    f.suspension[i] = a.suspension[i]+(b.suspension[i]-a.suspension[i])*t;
  f.steer = a.steer+(b.steer-a.steer)*t;
  return f;
  }

// `v` turned by the angle and about the axis of `turn` (Rodrigues)
Vec3 rotated(const Vec3& v, const Vec3& turn) {
  const float a = turn.length();
  if(a<1e-6f)
    return v;
  const Vec3  k = turn*(1.f/a);
  const float c = std::cos(a), s = std::sin(a);
  return v*c + Vec3::cross(k,v)*s + k*(Vec3::dot(k,v)*(1-c));
  }

// `f` moved on by `dt` s along its velocity and angular velocity (world), the velocity turning with the car: a car
// in a bend goes on round it
CarProfile::Frame ahead(const CarProfile::Frame& f, float dt) {
  CarProfile::Frame r = f;
  r.time     = f.time+dt;
  r.position = f.position+rotated(f.velocity,f.spin*(0.5f*dt))*dt;
  r.velocity = rotated(f.velocity,f.spin*dt);
  turned(f.rot,f.spin,dt,r.rot);
  return r;
  }

}

std::vector<uint8_t> CarState::pack(const CarProfile::Frame& f, uint8_t flags) {
  Writer w;
  w.f32(f.time).f32(f.position.x).f32(f.position.y).f32(f.position.z);
  for(int k=0; k<4; ++k)
    w.u16(uint16_t(fixed16(f.rot[k],32767.f)));
  w.u16(uint16_t(fixed16(f.velocity.x,100.f))).u16(uint16_t(fixed16(f.velocity.y,100.f))).u16(uint16_t(fixed16(f.velocity.z,100.f)));
  w.u16(uint16_t(fixed16(f.spin.x,1000.f))).u16(uint16_t(fixed16(f.spin.y,1000.f))).u16(uint16_t(fixed16(f.spin.z,1000.f)));
  for(int i=0; i<6; ++i)
    w.u16(uint16_t(std::clamp(std::lround(f.suspension[i]*100000.f),0l,65535l)));
  w.u16(uint16_t(fixed16(f.steer,100.f)));
  w.u32(f.keys).u8(flags);
  return w.take();
  }

bool CarState::unpack(const std::vector<uint8_t>& data, CarProfile::Frame& f, uint8_t& flags) {
  if(data.size()!=Size)
    return false;
  Reader r(data);
  f = CarProfile::Frame();
  f.time = r.f32();
  f.position.x = r.f32(); f.position.y = r.f32(); f.position.z = r.f32();
  for(int k=0; k<4; ++k)
    f.rot[k] = float(int16_t(r.u16()))/32767.f;
  normalize(f.rot);
  f.velocity.x = float(int16_t(r.u16()))/100.f;
  f.velocity.y = float(int16_t(r.u16()))/100.f;
  f.velocity.z = float(int16_t(r.u16()))/100.f;
  f.spin.x = float(int16_t(r.u16()))/1000.f;
  f.spin.y = float(int16_t(r.u16()))/1000.f;
  f.spin.z = float(int16_t(r.u16()))/1000.f;
  for(int i=0; i<6; ++i)
    f.suspension[i] = float(r.u16())/100000.f;
  f.steer = float(int16_t(r.u16()))/100.f;
  f.keys  = r.u32();
  flags   = r.u8();
  return r.ok() && std::isfinite(f.time) && std::isfinite(f.position.x) && std::isfinite(f.position.y)
         && std::isfinite(f.position.z);
  }

RemoteCar::RemoteCar(float rate) {
  // one and a half states behind, so that one lost state is bridged by the next, and a little for the jitter
  lag = 1.5f/std::max(rate,1.f)+0.03f;
  }

void RemoteCar::clear() {
  list.clear();
  synced = false;
  }

void RemoteCar::receive(const CarProfile::Frame& f, uint8_t flags, float now) {
  if(!list.empty() && f.time<=list.back().time)
    return;   // late: a newer one is here already
  const float sample = f.time-now;
  if(!synced || sample>offset)
    offset = sample;
  synced    = true;
  gotAt     = now;
  lastFlags = flags;
  list.push_back(f);
  while(list.size()>2 && list[1].time<f.time-Keep)
    list.pop_front();
  }

CarProfile::Frame RemoteCar::at(float now) const {
  if(list.empty())
    return CarProfile::Frame();
  const float t = now+offset-lag;
  if(t<=list.front().time)
    return list.front();
  if(t>=list.back().time)
    return ahead(list.back(),std::min(t-list.back().time,MaxAhead));
  auto b = std::upper_bound(list.begin(),list.end(),t,[](float v, const CarProfile::Frame& f){ return v<f.time; });
  auto a = b-1;
  const float span = b->time-a->time;
  return lerp(*a,*b,span>0 ? (t-a->time)/span : 0.f);
  }

Mat4 RemoteCar::pose(const CarProfile::Frame& f) {
  return CarProfile::poseOf(f);
  }

}
