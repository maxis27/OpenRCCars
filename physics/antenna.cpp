#include "antenna.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

#include "formats/pack.h"
#include "formats/scene.h"
#include "formats/settings.h"

using namespace rc;

namespace {

constexpr float Eps     = 1e-6f;   // 0x554380
constexpr float Step    = 0.003f;  // 0x554988: the longest step
constexpr float MinStep = 1e-4f;   // 0x554388: a rest shorter than this goes into the last step
constexpr float Gravity = -10.f;   // 0x44db50
constexpr float Push    = -100.f;  // 0x4ff260
constexpr float MaxRise = 0.466667f; // 0x554a84: the ceiling is looked for at most this far above the first link

Vec3 axis(const Mat4& m, int i) { return {m.m[0][i],m.m[1][i],m.m[2][i]}; }

Mat4 rotationOf(const Mat4& m) {
  Mat4 r = m;
  r.m[0][3] = r.m[1][3] = r.m[2][3] = 0;
  return r;
  }

Mat4 placed(const Mat4& rot, const Vec3& at) {
  Mat4 r = rot;
  r.m[0][3] = at.x; r.m[1][3] = at.y; r.m[2][3] = at.z;
  return r;
  }

// the rotation turning unit `a` onto unit `b` the shortest way (0x407620; half a turn round a side axis when they
// are opposite)
Mat4 turn(const Vec3& a, const Vec3& b) {
  const float c = std::clamp(Vec3::dot(a,b),-1.f,1.f);
  Vec3 ax = Vec3::cross(a,b);
  float s = ax.length();
  if(s<Eps) {
    if(c>0)
      return Mat4();
    ax = Vec3::cross(a,{1,0,0});
    if(ax.length()<Eps)
      ax = Vec3::cross(a,{0,1,0});
    ax = ax.normalized();
    s  = 0;
    }
  else {
    ax = ax*(1.f/s);
    }
  const float t = 1.f-c;
  Mat4 r;
  r.m[0][0] = c+ax.x*ax.x*t;      r.m[0][1] = ax.x*ax.y*t-ax.z*s; r.m[0][2] = ax.x*ax.z*t+ax.y*s;
  r.m[1][0] = ax.y*ax.x*t+ax.z*s; r.m[1][1] = c+ax.y*ax.y*t;      r.m[1][2] = ax.y*ax.z*t-ax.x*s;
  r.m[2][0] = ax.z*ax.x*t-ax.y*s; r.m[2][1] = ax.z*ax.y*t+ax.x*s; r.m[2][2] = c+ax.z*ax.z*t;
  return r;
  }

// between two poses of the car (0x4080f0): the parts mixed, the axes made square again
Mat4 between(const Mat4& a, const Mat4& b, float t) {
  Mat4 r;
  for(int y=0; y<4; ++y)
    for(int x=0; x<4; ++x)
      r.m[y][x] = a.m[y][x]+(b.m[y][x]-a.m[y][x])*t;
  const Vec3 X = axis(r,0).normalized();
  Vec3       Y = axis(r,1);
  Y = (Y-X*Vec3::dot(X,Y)).normalized();
  const Vec3 Z = Vec3::cross(X,Y);
  const Vec3 c[3] = {X,Y,Z};
  for(int i=0; i<3; ++i) {
    r.m[0][i] = c[i].x; r.m[1][i] = c[i].y; r.m[2][i] = c[i].z;
    }
  return r;
  }

Vec3 halve(const Vec3& p) { return {p.x*0.5f,p.y,p.z*0.5f}; }

// 0x500230: the slider over 100, 10 above 1000
float slider(const SliderFile& f, std::string_view key, float def) {
  if(!f.find(key))
    return def;
  const float v = float(f.raw(key));
  return v<0 ? 0.f : v>1000 ? 10.f : v*0.01f;
  }

}

Antenna::Settings Antenna::Settings::load(PackFile& pack, size_t car) {
  Settings s;
  const std::string name = "Car"+std::to_string(car+1);
  SliderFile f;
  bool       have = false;
  if(pack.find("Settings\\Antenna_"+name+".ini")!=nullptr) {
    const auto d = pack.read("Settings\\Antenna_"+name+".ini");
    f    = SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
    have = true;
    }
  else if(pack.find("Settings\\Antenna_"+name+".crs")!=nullptr) {
    f    = SliderFile::parseCrs(pack.read("Settings\\Antenna_"+name+".crs"));
    have = true;
    }
  if(have) {
    s.points = std::clamp(int32_t(f.raw("nPoints",s.points)),2,10);
    if(f.find("chainLength"))
      s.chainLength = std::clamp(float(f.raw("chainLength"))*0.01f,0.f,1.f);
    s.mass         = slider(f,"mass",s.mass);
    s.stiffness    = slider(f,"stiffness",s.stiffness);
    s.damping      = slider(f,"damping",s.damping);
    s.windFriction = slider(f,"windFriction",s.windFriction);
    s.volume       = slider(f,"volume",s.volume);
    }
  // 0x4fff70: "ARAC", "   1", "   0", "ARAC", the count (1..199), then angle, angle, height, and "ARAC" again
  const std::string cache = "CacheData\\car_antenna_"+name+"_H2F.data";
  if(pack.find(cache)!=nullptr) {
    const auto d = pack.read(cache);
    auto u32 = [&](size_t at) { uint32_t v = 0; if(at+4<=d.size()) std::memcpy(&v,d.data()+at,4); return v; };
    auto f32 = [&](size_t at) { float v = 0; if(at+4<=d.size()) std::memcpy(&v,d.data()+at,4); return v; };
    const uint32_t n = u32(16);
    if(d.size()>=20 && std::memcmp(d.data(),"ARAC",4)==0 && n>=1 && n<200 && d.size()>=20+12*size_t(n)+4) {
      for(uint32_t i=0; i<n; ++i)
        s.h2f.push_back({f32(20+12*i),f32(20+12*i+8)});
      }
    }
  return s;
  }

float Antenna::Settings::freeAngle() const {
  // without the table the push is none: 1 + sin(3/2 pi) = 0
  return h2f.empty() ? 4.712389f : h2f.back().angle;
  }

float Antenna::Settings::reach() const {
  return (h2f.empty() ? chainLength : h2f.back().height)+0.1f;
  }

float Antenna::Settings::angleUnder(float ceiling) const {
  if(h2f.empty())
    return freeAngle();
  // 0x4fe5f0: measured from the end of the first link, at most MaxRise above it
  const float link = points>1 ? chainLength/float(points-1) : chainLength;
  const float h    = std::clamp(ceiling-link,0.f,MaxRise)+link;
  if(h<h2f.front().height+Eps)
    return h2f.front().angle;
  if(h>h2f.back().height-Eps)
    return h2f.back().angle;
  auto it = std::upper_bound(h2f.begin(),h2f.end(),h,[](float v, const Height& e){ return v<e.height; });
  return (it==h2f.begin() ? it : it-1)->angle;
  }

bool Antenna::mountOf(const SbScene& scene, int32_t model, Vec3& out) {
  for(const char* name:{"ANTENNA","Antennaa"})
    for(auto& m:scene.meshes) {
      if(m.model!=model || m.name!=name || m.positions.empty())
        continue;
      Vec3 lo = m.positions[0], hi = m.positions[0];
      for(auto& p:m.positions) {
        lo = {std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
        hi = {std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
        }
      out = m.world.point({hi.x,lo.y,hi.z});
      return true;
      }
  return false;
  }

Antenna::Antenna(const Settings& settings, const Mat4& carPose, const Vec3& mount, const Mat4& rest)
  : set(settings), mount(mount), restRot(rotationOf(rest)) {
  const size_t n = size_t(std::max(set.points,2));
  link = set.chainLength/float(n-1);
  // 0x4ff2d0: straight up from the mount, at rest
  cur.resize(n);
  const Vec3 base = halve(carPose.point(mount));
  for(size_t i=0; i<n; ++i)
    cur[i].pos = base+Vec3{0,link*float(i),0};
  cur[0].frame = placed(rotationOf(carPose)*restRot,cur[0].pos);
  rebuildFrames(cur);
  prev       = cur;
  lastCar    = carPose;
  mountWorld = carPose.point(mount);
  // and lets it settle for 0.01 s
  step(0.01f,carPose);
  }

void Antenna::rebuildFrames(std::vector<Point>& s) const {
  // 0x44e280, 0x44e2f0: each frame the one before turned onto the link that comes into its point
  for(size_t i=1; i<s.size(); ++i) {
    const Mat4 before = rotationOf(s[i-1].frame);
    const Vec3 dir    = (s[i].pos-s[i-1].pos).normalized();
    s[i].frame = placed(turn(axis(before,0).normalized(),dir)*before,s[i].pos);
    }
  }

void Antenna::step(float dt, const Mat4& carPose, std::optional<float> ceiling) {
  if(dt<=0 || cur.size()<2)
    return;
  const float angle = ceiling ? set.angleUnder(*ceiling) : set.freeAngle();
  const Vec3  zAxis = axis(carPose,2).normalized();   // 0x40cbf0 of the car's own matrix, not the step's
  float left = dt;
  while(left>0) {
    float h = std::min(Step,left);
    left -= h;
    if(left<MinStep) {
      h   += left;
      left = 0;
      }
    const float t = (dt-left)/dt;
    substep(h,between(lastCar,carPose,t),zAxis,angle);
    }
  lastCar    = carPose;
  mountWorld = carPose.point(mount);
  }

void Antenna::substep(float h, const Mat4& car, const Vec3& zAxis, float angle) {
  const size_t n = cur.size();
  // 0x4fe9d0: where the mount points are now
  std::vector<Point> out = cur;
  const Vec3 up = axis(car,1).normalized();
  out[0].pos   = halve(car.point(mount));
  out[1].pos   = out[0].pos+up*link;
  out[0].frame = placed(rotationOf(car)*restRot,out[0].pos);

  auto fixed = [](size_t i) { return i<2; };
  const float invMass = set.mass>Eps ? 1.f/set.mass : 0.f;

  // 0x44cd00: the mount points' speed and acceleration from their last steps
  for(size_t i=0; i<n; ++i)
    if(fixed(i)) {
      const Vec3 v = (cur[i].pos-prev[i].pos)*(1.f/h);
      cur[i].acc = (v-prev[i].vel)*(1.f/h);
      cur[i].vel = v;
      }
  // 0x44cdd0: the weight
  for(size_t i=0; i<n; ++i)
    if(!fixed(i))
      cur[i].force = Vec3{0,Gravity,0}*set.mass;
  // 0x44ce80: the still air against the point's speed, by how much of the link faces it
  for(size_t i=1; i<n; ++i) {
    if(fixed(i))
      continue;
    const Vec3  d   = cur[i].pos-cur[i-1].pos;
    const float len = d.length();
    const Vec3  rel = cur[i].vel*-1.f;
    const float rr  = Vec3::dot(rel,rel);
    if(rr<1e-12f || len<Eps)
      continue;
    const float c = std::clamp(Vec3::dot(d,rel)/(len*std::sqrt(rr)),-1.f,1.f);
    const float k = std::sqrt(set.volume/len)*len*std::sqrt(1.f-c*c)*set.windFriction;
    cur[i].force += rel*k;
    }
  // 0x4ff260: the push on the tip
  if(!fixed(n-1))
    cur[n-1].force += zAxis*((std::sin(angle)+1.f)*Push);
  // 0x44d080: bending back towards the link before
  for(size_t i=1; i<n; ++i) {
    if(fixed(i))
      continue;
    const Vec3  d   = cur[i].pos-cur[i-1].pos;
    const float len = d.length();
    if(len<Eps)
      continue;
    const Vec3  dn   = d*(1.f/len);
    const Vec3  rest = axis(cur[i-1].frame,0).normalized();
    const float a    = std::atan2(Vec3::cross(rest,dn).length(),Vec3::dot(rest,dn));
    const float k    = a*set.stiffness/len;
    Vec3 side = rest-dn;
    side = side-dn*Vec3::dot(side,dn);
    if(side.length()<Eps)
      continue;
    side = side.normalized();
    cur[i].force   += side*k;
    cur[i-1].force += side*-k;
    }
  // 0x44d6b0: damping the motion against the frame of the point before
  for(size_t i=1; i<n; ++i) {
    if(fixed(i))
      continue;
    const float len = (cur[i].pos-cur[i-1].pos).length();
    if(len<Eps)
      continue;
    const Vec3 carried = (cur[i-1].frame*prev[i-1].frame.inverse()).point(prev[i].pos);
    const Vec3 v       = (cur[i].pos-carried)*((1.f/h)/len);
    cur[i].force   += v*-set.damping;
    cur[i-1].force += v*set.damping;
    }

  // 0x44dce0, 0x44dd80: the links keep their lengths. Along the chain from the last mount point to the tip, link k
  // between points k and k+1 (d = p_k - p_k+1) gets a multiplier from
  //   (w_k + w_k+1) |d_k|^2 l_k - w_k (d_k.d_k-1) l_k-1 - w_k+1 (d_k.d_k+1) l_k+1 = d_k.(a_k+1 - a_k) - |v_k - v_k+1|^2
  // (w the inverse masses, a the accelerations without the links), solved by 0x44e160
  const size_t first = 1;
  const size_t m     = n-first;      // points in the system
  if(m>=2) {
    std::vector<float> w(m), lower(m-1), diag(m-1), upper(m-1), rhs(m-1);
    std::vector<Vec3>  d(m-1), a(m);
    for(size_t j=0; j<m; ++j) {
      const size_t i = first+j;
      w[j] = fixed(i) ? 0.f : invMass;
      a[j] = fixed(i) ? cur[i].acc : cur[i].force*invMass;
      }
    for(size_t k=0; k+1<m; ++k)
      d[k] = cur[first+k].pos-cur[first+k+1].pos;
    for(size_t k=0; k+1<m; ++k) {
      const Vec3 dv = cur[first+k].vel-cur[first+k+1].vel;
      diag[k]  = (w[k]+w[k+1])*Vec3::dot(d[k],d[k]);
      lower[k] = k>0   ? -Vec3::dot(d[k],d[k-1])*w[k]   : 0.f;
      upper[k] = k+2<m ? -Vec3::dot(d[k],d[k+1])*w[k+1] : 0.f;
      rhs[k]   = Vec3::dot(d[k],a[k+1]-a[k])-Vec3::dot(dv,dv);
      }
    // Thomas algorithm
    const size_t links = m-1;
    std::vector<float> c(links), r(links), l(links);
    bool ok = true;
    for(size_t k=0; k<links && ok; ++k) {
      const float den = diag[k]-(k>0 ? lower[k]*c[k-1] : 0.f);
      if(std::abs(den)<1e-12f) {
        ok = false;
        break;
        }
      c[k] = upper[k]/den;
      r[k] = (rhs[k]-(k>0 ? lower[k]*r[k-1] : 0.f))/den;
      }
    if(ok) {
      for(size_t k=links; k-->0;)
        l[k] = r[k]-(k+1<links ? c[k]*l[k+1] : 0.f);
      for(size_t j=0; j<m; ++j) {
        const size_t i = first+j;
        if(fixed(i))
          continue;
        Vec3 f = cur[i].force;
        if(j>0)
          f += d[j-1]*-l[j-1];
        if(j+1<m)
          f += d[j]*l[j];
        cur[i].acc = f*invMass;
        }
      }
    }

  // 0x44d8e0: half the step's acceleration into the speed, the speed into the place
  for(size_t i=0; i<n; ++i)
    if(!fixed(i)) {
      out[i].vel = cur[i].vel+cur[i].acc*(0.5f*h);
      out[i].pos = cur[i].pos+out[i].vel*h;
      }
  // 0x44d970: back at the links' lengths, along them as fast as the point before
  for(size_t i=1; i<n; ++i) {
    if(fixed(i))
      continue;
    const Vec3  dir = (out[i].pos-out[i-1].pos).normalized();
    const float len = (cur[i].pos-cur[i-1].pos).length();
    out[i].pos = out[i-1].pos+dir*len;
    const Vec3 v = out[i].vel;
    out[i].vel = v-dir*Vec3::dot(dir,v)+dir*Vec3::dot(dir,out[i-1].vel);
    }
  rebuildFrames(out);
  prev = std::move(cur);
  cur  = std::move(out);
  }

std::vector<Mat4> Antenna::joints() const {
  std::vector<Mat4> r;
  if(cur.size()<2)
    return r;
  // 0x4ff0c0: joint j on point j, turned as the frame of the point after it; x and z doubled back about the mount
  const Vec3 p0 = cur[0].pos;
  for(size_t j=0; j+1<cur.size(); ++j) {
    const Vec3 p = cur[j].pos;
    const Vec3 at = {2.f*p0.x+(p.x-p0.x),p.y,2.f*p0.z+(p.z-p0.z)};
    r.push_back(placed(rotationOf(cur[j+1].frame),at));
    }
  return r;
  }
