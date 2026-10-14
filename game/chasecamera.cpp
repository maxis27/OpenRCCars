#include "chasecamera.h"

#include "camera.h"
#include "formats/collision.h"
#include "formats/settings.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>

using namespace rc;
using namespace Tempest;

namespace {

constexpr float Pi       = 3.14159265358979f;
constexpr float DegToRad = Pi/180.f;
constexpr float Eps      = 1e-6f;
constexpr float Huge     = 2e6f;   // RCCars.exe's "no limit"

// RCCars.exe 0x49d7e0: `from` moved toward `to` by at most rate*dt
float toward(float from, float to, float rate, float dt) {
  const float s = rate*dt;
  if(std::abs(from-to)<s)
    return to;
  return from<to ? from+s : from-s;
  }

Vec3 flat(Vec3 v) {
  v.y = 0;
  return v;
  }

}

ChaseCamera::Settings ChaseCamera::Settings::load(const std::function<std::vector<uint8_t>(const std::string&)>& read) {
  Settings s;
  try {
    const auto d  = read("Settings\\Camera.ini");
    const auto sl = SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
    // 0x4f9da0: the 1..100 sliders mapped by hand, out of range ones held at the ends
    auto lin = [&sl](std::string_view key, int def, float lo, float hi) {
      const int v = std::clamp(sl.raw(key,def),1,100);
      return lo + float(v-1)*(hi-lo)/99.f;
      };
    s.distXZ     = lin("defDistXZ",30,0.5f,1.5f);
    s.distY      = lin("defDistY", 30,0.1f,1.0f);
    s.accDistY   = lin("accDistY", 33,0.1f,2.0f);
    s.accAlpha   = lin("accAlpha", 50,0.f, 1.f);
    s.visTurn    = lin("VisTurn",  70,-30.f,30.f);
    // the CDT_* ones as their FValues say (CDT_AngleUp_SPEED_DIR_ADD sits at 151 of 1..100)
    s.angleUp    = sl.real("CDT_AngleUp",              s.angleUp);
    s.angleUpSpd = sl.real("CDT_AngleUpSpeed",         s.angleUpSpd);
    s.dirAdd     = sl.real("CDT_AngleUp_DIR_ADD",      s.dirAdd);
    s.dirAddSpd  = sl.real("CDT_AngleUp_SPEED_DIR_ADD",s.dirAddSpd);
    }
  catch(const std::exception& e) {
    Log::e("Settings\\Camera.ini: ",e.what());
    }
  return s;
  }

ChaseCamera::ChaseCamera() : cfg(Settings()) {}

float ChaseCamera::Spring::step(float v, float dt) const {
  if(std::abs(v-target)<tol+Eps)
    return 0;
  const float lo = target-below, hi = target+above;
  float w = toward(v, v<lo ? lo : (v>hi ? hi : v), maxRate, dt);
  if(w<hi+Eps && w>lo-Eps) {
    float speed = rate;
    if(std::abs(exp)>=Eps) {
      // 0x49d740
      const float band = v>=target ? above : below;
      float f = 0;
      if(band>=Eps)
        f = std::pow(std::min(std::abs(target-w),band)/band,exp);
      speed *= f;
      }
    const float s = std::clamp(speed*dt-std::abs(w-v),0.f,Huge);
    w = toward(w,target,s,1.f);
    }
  return w-v;
  }

Vec3 ChaseCamera::heading(const Car& car) {
  // 0x500840: the nose laid flat; with the nose up (less than 30 degrees from straight up) the nose and the back
  // half of the roof together
  Vec3 f = car.pose.vector({0,0,1}).normalized();
  if(std::acos(std::clamp(f.y,-1.f,1.f))<30.f*DegToRad)
    f = (f-car.pose.vector({0,1,0}).normalized())*0.5f;
  f = flat(f);
  if(f.length()<Eps)
    return lastHeading;
  return lastHeading = f.normalized();
  }

void ChaseCamera::reset(const Car& car) {
  valid      = false;
  extraSpeed = 0;
  extraBoost = 0;
  swing      = 0;
  lookUp     = 0;
  update(car,Huge);
  }

void ChaseCamera::update(const Car& car, float dt) {
  const Vec3 f    = heading(car);
  const Vec3 side = Vec3::cross({0,1,0},f);   // any side will do: the camera does not care which way round
  const Vec3 o    = car.centre;
  if(!valid) {
    eye   = o - f*cfg.distXZ + Vec3{0,cfg.distY,0};
    valid = true;
    dt    = Huge;
    }

  // 0x500a80: this frame's springs
  const float base  = cfg.distXZ;
  const float speed = car.velocity.length();
  float       add   = 0;
  if(speed>=4.f)
    add = speed>8.f ? base*0.5f : base*0.5f*(speed-4.f)*0.25f;
  extraSpeed = toward(extraSpeed,add,1.f,dt);
  extraBoost = car.boost ? toward(extraBoost,base*0.2f,0.5f,dt) : toward(extraBoost,0.f,0.3f,dt);

  Spring dist {base+extraSpeed+extraBoost, 0, Huge, Huge, Huge, Huge, 0};
  Spring high {cfg.distY, 0, cfg.distY*0.75f, cfg.distY*0.75f, cfg.accDistY, Huge, 0};
  const float alphaRate = car.spin/DegToRad<180.f ? cfg.accAlpha*200.f : 30.f;
  Spring alpha{0, 0, 20.f, 20.f, alphaRate, alphaRate, 2.f};

  // 0x471ca0: the eye in the car's frame, pulled, and put back
  const Vec3  p = eye-o;
  const float x = Vec3::dot(p,side), y = p.y, z = Vec3::dot(p,f);
  const float d = std::sqrt(x*x+z*z);
  const float a = d<=Eps ? 0.f : -std::atan2(x,-z)/DegToRad;   // 0 straight behind
  const float nd = d + dist.step(d,dt);
  const float ny = y + high.step(y,dt);
  const float na = (a + alpha.step(a,dt))*DegToRad;
  eye = o + side*(-nd*std::sin(na)) + Vec3{0,ny,0} + f*(-nd*std::cos(na));
  }

void ChaseCamera::apply(const Car& car, const rc::CollisionMesh* coll, float dt, Camera& camera) {
  // 0x501180 looks at the car object's position (+0xf8), taken here to be the body's, like the frame (+0x5884)
  const Vec3  at  = car.centre;
  const Vec3  to  = at-eye;
  const float len = to.length();

  // 0x500e60: the ground between the car and the eye swings the eye up round the car
  bool hidden = false;
  if(coll!=nullptr && !coll->empty()) {
    const Vec3 from = at+Vec3{0,0.15f,0};
    const Vec3 end  = eye-Vec3{0,0.06f,0};
    hidden = coll->raycast(from,end-from,1.f).has_value();
    }
  swing = toward(swing, hidden ? cfg.angleUp : 0.f, cfg.angleUpSpd, dt);
  const float k = cfg.angleUp<Eps ? 0.f : std::clamp(swing/cfg.angleUp,0.f,1.f);
  lookUp = toward(lookUp, swing<0 ? 0.f : cfg.dirAdd*k, cfg.dirAddSpd, dt);

  // the line to the car turned down by `swing` round the camera's side axis: the eye goes up by as much
  Vec3 fwd = len>Eps ? to*(1.f/len) : lastHeading;
  Vec3 hor = flat(fwd);
  hor = hor.length()<Eps ? lastHeading : hor.normalized();
  const float pitch = std::asin(std::clamp(fwd.y,-1.f,1.f)) - swing*DegToRad;
  fwd = hor*std::cos(pitch) + Vec3{0,std::sin(pitch),0};

  camera.position = at - fwd*len;
  camera.lookAt(at);
  // 0x501180: the view looks up by VisTurn and the extra of the swing (no clamp: pitch stays well inside +-90)
  camera.pitch += cfg.visTurn + lookUp;
  }
