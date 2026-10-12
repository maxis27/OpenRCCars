#include "carparams.h"

#include <algorithm>
#include <cmath>

using namespace rc;

namespace {

SliderFile sliders(PackFile& pack, const std::string& name) {
  if(pack.find(name)==nullptr)
    return {};
  auto d = pack.read(name);
  return SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
  }

// slider position as 0..1
float unit(const SliderFile& f, std::string_view key, float def) {
  auto s = f.find(key);
  if(s==nullptr || s->max<=s->min)
    return def;
  return std::clamp(float(s->value-s->min)/float(s->max-s->min),0.f,1.f);
  }

// RCCars.exe: slider position 1..100 mapped linearly onto lo..hi, whatever range the file declares
float mapped(const SliderFile& f, std::string_view key, float lo, float hi, float def) {
  auto s = f.find(key);
  if(s==nullptr || s->value<1 || s->value>100)
    return def;
  return lo + float(s->value-1)*(hi-lo)/99.f;
  }

Spline spline(PackFile& pack, const std::string& name) {
  if(auto e = pack.find("Splines\\"+name+".bspl"))
    return Spline::parse(pack.read(*e));
  return {};
  }

}

CarParams CarParams::load(PackFile& pack, int32_t car, int32_t upgrade) {
  static const char* const UpgradeKey[] = {"OverkillUpgrades","BuggyUpgrades","HummerUpgrades"};
  static const char* const Engine[]     = {"OVERKILL","BUGGY","HUMMER"};

  CarParams p;
  p.car     = std::clamp(car,1,3);
  p.upgrade = std::clamp(upgrade,0,3);
  const std::string n = std::to_string(p.car);

  p.accel = spline(pack,Engine[p.car-1]);
  if(p.accel.empty())
    p.accel = Spline({{0,6.f},{32.f,0}});

  KeySections up;
  if(auto e = pack.find("Scripts\\upgrades.ini")) {
    auto d = pack.read(*e);
    up = KeySections::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
    }
  auto pick = [&](const char* section, int32_t c, int32_t u, std::string_view suffix = "") {
    auto& v = up.get(section,std::string(UpgradeKey[c-1])+std::string(suffix));
    return size_t(u)<v.size() ? v[size_t(u)] : 1.f;
    };
  p.resonator  = pick("RESONATORS",p.car,p.upgrade);
  p.accelScale = pick("RESONATORS",p.car,p.upgrade,"_SPL_OY");
  const float tires = pick("TIRES",p.car,p.upgrade);
  p.gripFront *= tires;
  p.gripRear  *= tires;

  // engine<N>.ini: speedBaseMax 33/38/30 and speedBoostMax 38/45/37 km/h (the game adds 0.1), boostSpeedUp
  // 0.1..1.5 and boostSpeedDown 1..10 units/s
  auto engine = [&](int32_t c) { return sliders(pack,"Settings\\engine"+std::to_string(c)+".ini"); };
  const auto eng = engine(p.car);
  p.topSpeed      = (mapped(eng,"speedBaseMax", 1,100,33)+0.1f)/3.6f*p.resonator;
  p.boostTopSpeed = (mapped(eng,"speedBoostMax",1,100,38)+0.1f)/3.6f*p.resonator;
  auto charge = [&](int32_t c, int32_t u) {
    return mapped(engine(c),"boostSpeedUp",0.1f,1.5f,1.f)*pick("BOOSTERS",c,u);
    };
  // the tank holds 50 units for the slowest refill of all cars (Buggy, stock) .. 100 for the fastest (Hummer, top
  // boosters)
  const float x = charge(p.car,p.upgrade), lo = charge(2,0), hi = charge(3,3);
  p.boostTank   = hi>lo ? std::round(50.f + 50.f*std::clamp((x-lo)/(hi-lo),0.f,1.f)) : 50.f;
  p.boostCharge = x*0.95f;
  p.boostDrain  = mapped(eng,"boostSpeedDown",1,10,5.f);
  p.jumpSpeed   = p.car==2 ? 3.3f : 3.f;

  // steering<N>.ini AngleSteer 30 (0..100 degrees), steering<N>.bspl 20 degrees per unit of steering position
  const auto steer = sliders(pack,"Settings\\steering"+n+".ini");
  p.steerAngle = mapped(steer,"AngleSteer",0,100,29.3f);
  p.steerCurve = spline(pack,"steering"+n);
  if(p.steerCurve.empty())
    p.steerCurve = Spline({{0,0},{2.f,40.f}});

  // springs<N>.ini: kPos 64/43/64 stiffness, kSpeed 36/36/25 damping, length 43/48/43 rest length (mm),
  // coeffMomentOZ/OX roll/pitch inertia
  const auto spr = sliders(pack,"Settings\\springs"+n+".ini");
  p.suspStiffness   = 4.f*float(spr.raw("kPos",64));
  const float crit  = 2.f*std::sqrt(p.suspStiffness); // critical damping of one wheel's share
  p.suspDamping     = crit*unit(spr,"kSpeed",0.36f)*1.2f;
  p.suspCompression = p.suspDamping*0.7f;
  p.suspRest        = float(spr.raw("length",43))*0.001f;
  p.suspTravel      = p.suspRest*0.75f;
  p.inertiaRoll     = float(spr.raw("coeffMomentOZ",44))/25.f;
  p.inertiaPitch    = float(spr.raw("coeffMomentOX",37))/25.f;

  // tires<N>.ini: CoeffFrontTires / CoeffRearTires 1..10 (the game: 0.1..1)
  const auto tir = sliders(pack,"Settings\\tires"+n+".ini");
  p.gripFront *= float(tir.raw("CoeffFrontTires",7))/7.f;
  p.gripRear  *= float(tir.raw("CoeffRearTires", 7))/7.f;
  // coeffDeepSand 7, coeffWater 20 (Hummer 7): 0..1 per wheel
  p.deepSandDrag = mapped(tir,"coeffDeepSand",0,1,0.06f);
  p.waterDrag    = mapped(tir,"coeffWater",   0,1,0.19f);

  // Water.ini: speedDropCenter 125/500, speedDropOther 76/500, DampingVelocity2 950/1000. RCCars.exe does not read
  // this file; how these would apply is our guess.
  const auto wtr = sliders(pack,"Settings\\Water.ini");
  p.waterDropBody   = unit(wtr,"speedDropCenter",0.25f);
  p.waterDropWheels = unit(wtr,"speedDropOther",0.15f);
  p.waterDamping    = unit(wtr,"DampingVelocity2",0.95f);

  // resistance<N>.ini: coeffFrictionBearings 6 (0..10 m/s^2), coeffAirResistance 33 (0..0.1 1/m)
  const auto res = sliders(pack,"Settings\\resistance"+n+".ini");
  p.rolling = mapped(res,"coeffFrictionBearings",0,10,0.5f);
  p.airDrag = mapped(res,"coeffAirResistance",0,0.1f,0.03f);

  // cdt<N>.ini: CenterMassOY 17 (the game: 0..0.2 m)
  const auto cdt = sliders(pack,"Settings\\cdt"+n+".ini");
  p.comHeight = unit(cdt,"CenterMassOY",0.17f);
  return p;
  }

float CarParams::steerRate(float speed) {
  return std::clamp(4.5f-0.5f*speed,1.f,4.5f);
  }

float CarParams::acceleration(float v, float throttle, bool boost) const {
  throttle = std::abs(throttle);
  if(throttle<=0 || v>=(boost ? boostTopSpeed : topSpeed))
    return 0;
  // boosting stretches the curve to the higher top speed
  const float ratio = boost ? boostTopSpeed/topSpeed : 1.f;
  float a = accel.at(v*3.6f/(throttle*ratio*resonator));
  if(boost)
    a *= boostAccel;
  return std::max(a,0.f)*accelScale;
  }
