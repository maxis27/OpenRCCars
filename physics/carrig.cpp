#include "carrig.h"

#include <algorithm>
#include <cmath>

using namespace rc;

namespace {

constexpr float Deg = 57.2957795f;

// turns by `degrees` about the model axis `axis` (0 x, 1 y, 2 z) through `at`
Mat4 turnAbout(const Vec3& at, int axis, float degrees) {
  return Mat4::translate(at)*Mat4::rotate(axis,degrees)*Mat4::translate(at*-1.f);
  }

// the rotation taking the unit vector `a` to the unit vector `b`
Mat4 rotation(const Vec3& a, const Vec3& b) {
  const Vec3  v = Vec3::cross(a,b);
  const float c = Vec3::dot(a,b);
  Mat4 r;
  if(c<=-0.9999f)
    return r;
  const float k = 1.f/(1.f+c);
  r.m[0][0] = c+v.x*v.x*k;     r.m[0][1] = v.x*v.y*k-v.z;   r.m[0][2] = v.x*v.z*k+v.y;
  r.m[1][0] = v.y*v.x*k+v.z;   r.m[1][1] = c+v.y*v.y*k;     r.m[1][2] = v.y*v.z*k-v.x;
  r.m[2][0] = v.z*v.x*k-v.y;   r.m[2][1] = v.z*v.y*k+v.x;   r.m[2][2] = c+v.z*v.z*k;
  return r;
  }

// stretches by `k` along the unit vector `u`
Mat4 stretch(const Vec3& u, float k) {
  Mat4 r;
  const float uv[3] = {u.x,u.y,u.z};
  for(int y=0; y<3; ++y)
    for(int x=0; x<3; ++x)
      r.m[y][x] += (k-1.f)*uv[y]*uv[x];
  return r;
  }

}

Mat4 CarRig::Pose::part(std::string_view name, const Vec3& pivot) const {
  for(auto& [n,m]:nodes)
    if(n==name)
      return m*Mat4::translate(pivot);
  return Mat4::translate(pivot);
  }

CarRig::CarRig(const CarGeometry& g, const CarParams& prm)
  :geom(g), axleLength(prm.axleLength), armUp(prm.armUpMax), armDown(prm.armDownMax) {
  for(auto& st:geom.struts)
    if(st.axis.length()>0)
      st.end = st.pivot+st.axis*prm.strutLength;
  // 0x504820 lets the Overkill's axles swing 30 degrees down, 0x5068e0 the Hummer's 20
  hummer   = prm.car==3;
  droopMax = hummer ? 20.f : 30.f;
  // 0x5065c0 sweeps the Hummer's axles through -90..90 degrees when it loads the car, and takes the middle axle's
  // travel from the front one: how far its left wheel is from the node's height at 20 degrees
  middleTravel = 0;
  for(auto& a:geom.axles)
    if(!a.middle && a.left>=0 && geom.wheels[size_t(a.left)].front) {
      const Vec3 d = geom.wheels[size_t(a.left)].center-a.pivot;
      middleTravel = std::abs(d.y*std::cos(droopMax/Deg)-std::abs(d.z)*std::sin(droopMax/Deg));
      }
  }

CarRig::Pose CarRig::pose(const std::vector<Wheel>& in) const {
  Pose ret;
  auto lift = [&](int i) { return i>=0 && size_t(i)<in.size() ? in[size_t(i)].lift : 0.f; };
  auto node = [&](const std::string& name) -> const Mat4* {
    for(auto& [n,m]:ret.nodes)
      if(n==name)
        return &m;
    return nullptr;
    };

  // rigid axles: swing and roll about the node
  for(auto& a:geom.axles) {
    const float l     = lift(a.left), r = lift(a.right);
    const int   w     = a.left>=0 ? a.left : a.right;
    const Vec3  d     = w>=0 ? geom.wheels[size_t(w)].center-a.pivot : Vec3{};
    const float track = std::abs(d.x), lever = std::abs(d.z);
    // the Hummer looks the angles up in tables of the wheel's height over the angle (0x507050), which is
    // asin(height / distance from the node); the Overkill uses atan(4 * difference) and atan(drop / lenAxe)
    auto        ik    = [](float h, float len) { return len>0 ? std::asin(std::clamp(h/len,-1.f,1.f))*Deg : 0.f; };
    const float roll  = hummer ? ik((l-r)*0.5f,track) : std::atan((l-r)*4.f)*Deg;
    Mat4 m;
    if(a.middle) {
      const float y = std::clamp((l+r)*0.5f,-middleTravel,middleTravel);
      m = Mat4::translate({0,y,0})*turnAbout(a.pivot,2,roll);
      } else {
      // + = the wheels go down; the front axle's wheels are ahead of its node, the rear one's behind
      const float drop  = -(l+r)*0.5f;
      const float droop = std::clamp(hummer ? ik(drop,lever) : std::atan(drop/axleLength)*Deg,-90.f,droopMax);
      const bool  ahead = d.z>0;
      m = turnAbout(a.pivot,0,ahead ? droop : -droop)*turnAbout(a.pivot,2,roll);
      }
    ret.nodes.emplace_back(a.name,m);
    }

  // the Buggy's arms
  for(size_t i=0; i<geom.wheels.size(); ++i) {
    auto& w = geom.wheels[i];
    if(w.armUp.empty())
      continue;
    const Vec3  v     = w.carrierPivot-w.upPivot;
    const float side  = v.x<0 ? -1.f : 1.f;
    const float out   = std::abs(v.x), len = std::sqrt(out*out+v.y*v.y);
    const float a0    = std::atan2(v.y,out);
    const float s     = len>0 ? std::clamp((lift(int(i))+v.y)/len,-1.f,1.f) : 0.f;
    const float raise = std::clamp((std::asin(s)-a0)*Deg,-armDown,armUp);
    const Mat4  up    = turnAbout(w.upPivot,2,side*raise);
    ret.nodes.emplace_back(w.armUp,up);
    ret.nodes.emplace_back(w.armDown,turnAbout(w.downPivot,2,side*raise));
    // the carrier only moves with the end of the upper arm
    ret.nodes.emplace_back(w.carrier,Mat4::translate(up.point(w.carrierPivot)-w.carrierPivot));
    }

  // wheels, with their steering supports
  ret.wheels.resize(geom.wheels.size());
  for(size_t i=0; i<geom.wheels.size(); ++i) {
    auto&       w     = geom.wheels[i];
    const float steer = i<in.size() ? in[i].steer : 0.f;
    const float spin  = i<in.size() ? in[i].spin  : 0.f;
    Mat4 base;
    if(!w.carrier.empty())
      base = *node(w.carrier);
    else if(w.axle>=0)
      base = *node(geom.axles[size_t(w.axle)].name);
    if(!w.support.empty()) {
      base = base*turnAbout(w.supportPivot,1,steer);
      ret.nodes.emplace_back(w.support,base);
      ret.wheels[i] = base*Mat4::translate(w.center)*Mat4::rotate(0,spin);
      } else {
      ret.wheels[i] = base*Mat4::translate(w.center)*Mat4::rotate(1,steer)*Mat4::rotate(0,spin);
      }
    }

  // springs: turned and stretched to reach their end where it went
  for(auto& st:geom.struts) {
    const Mat4* a = node(st.anchor);
    if(a==nullptr)
      continue;
    const Vec3  d0 = st.end-st.pivot;
    const Vec3  d  = a->point(st.end)-st.pivot; // before nodes grows
    const float l0 = d0.length(), l = d.length();
    if(l0<1e-5f || l<1e-5f)
      continue;
    const Vec3 u0 = d0*(1.f/l0);
    ret.nodes.emplace_back(st.name,Mat4::translate(st.pivot)*rotation(u0,d*(1.f/l))*stretch(u0,l/l0)*
                                   Mat4::translate(st.pivot*-1.f));
    }
  return ret;
  }
