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

  // springs<N>.ini (0x4f91b0): length 43/48/43 -> 0.218/0.243/0.218 m, kPos 64/43/64 -> sag 0.119/0.092/0.119 m,
  // kSpeed 36/36/25 -> 4.18/4.18/3.18 N s/m, touchPoint 11 -> 0.101 m, coeffMomentOX 37, coeffMomentOZ 44
  const auto spr = sliders(pack,"Settings\\springs"+n+".ini");
  p.springLength  = mapped(spr,"length",0.01f,0.5f,0.218f);
  p.springMin     = p.springLength*mapped(spr,"lenMin",0.1f,0.9f,0.1f);
  p.springMax     = p.springLength*mapped(spr,"lenMax",1.1f,2.f,1.1f);
  p.springSag     = (p.springLength-p.springMin)*mapped(spr,"kPos",0.1f,0.9f,0.61f);
  p.springDamping = mapped(spr,"kSpeed",1.f,10.f,4.18f);
  p.springMount   = {mapped(spr,"dx",0,1,0),mapped(spr,"touchPoint",0,1,0.101f),mapped(spr,"dz",0,1,0)};
  p.springPitch   = mapped(spr,"coeffMomentOX",0.5f,2.f,1.05f);
  p.springRoll    = mapped(spr,"coeffMomentOZ",1.f,3.f,1.87f);
  // lenAxe 27/26/27 -> 0.089/0.088/0.089 m, angleProcUpMax 0/40/0 and angleProcDownMax 0/30/0 -> 35.5 and 26.4
  // degrees for the Buggy
  p.axleLength    = mapped(spr,"lenAxe",0.05f,0.2f,0.089f);
  p.armUpMax      = mapped(spr,"angleProcUpMax",0,90,0);
  p.armDownMax    = mapped(spr,"angleProcDownMax",0,90,0);
  // WheelsVisual.crs (0x504470, shipped only compiled): SpringLength 77 -> 0.0791 m
  if(auto e = pack.find("Settings\\WheelsVisual.crs"))
    p.strutLength = mapped(SliderFile::parseCrs(pack.read(*e)),"SpringLength",0.01f,0.1f,p.strutLength);

  // tires<N>.ini (0x4f7fa0): CoeffFrontTires / CoeffRearTires 7/9/7 of 1..10 -> 0.7/0.9/0.7, angleDriftOn 10 -> 10
  // degrees, coeffDriftRear 49 -> 0.56
  const auto tir = sliders(pack,"Settings\\tires"+n+".ini");
  auto grip = [&](std::string_view key) {
    auto s = tir.find(key);
    return s!=nullptr && s->value>=1 && s->value<=10 ? 0.1f*float(s->value) : 0.7f;
    };
  p.gripFront  = grip("CoeffFrontTires")*tires;
  p.gripRear   = grip("CoeffRearTires") *tires;
  p.driftAngle = std::clamp(float(tir.raw("angleDriftOn",10)),1.f,45.f);
  p.driftGrip  = 1.f-(mapped(tir,"coeffDriftRear",1,100,49)-1.f)/110.f;
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

  // cdt<N>.ini (0x4f8400): CdtRadWheel 68/44/65 -> 0.071/0.049/0.068 m, coeffRadBackWheels 1/38/1 -> rear wheels
  // 1/1.37/1 times larger, CenterMassOY 17 -> 0.032 m. The body is 3 x 3 spheres: CdtRadUp<i> (0.01..0.3 m) at
  // CdtDeltaX..Z<i> (-0.3..0.3 m; the Buggy's z is CdtDeltaZ_CENTRAL<i>), once in the middle and once shifted by
  // shiftRoofBaseLefter (0..0.5 m) to each side. The Hummer has 6 wheels, the others 4.
  const auto cdt = sliders(pack,"Settings\\cdt"+n+".ini");
  p.wheelRadius     = mapped(cdt,"CdtRadWheel",0.01f,0.1f,0.071f);
  p.rearWheelRadius = p.wheelRadius*mapped(cdt,"coeffRadBackWheels",1.f,2.f,1.f);
  p.comHeight       = mapped(cdt,"CenterMassOY",0.f,0.2f,0.032f);
  const float side  = cdt.real("shiftRoofBaseLefter",0.07f);
  p.body.clear();
  for(int s=0; s<3; ++s)
    for(int i=1; i<=3; ++i) {
      const std::string k = i==1 ? "" : std::to_string(i);
      Sphere sp;
      sp.radius = mapped(cdt,"CdtRadUp"+k,0.01f,0.3f,0.08f);
      sp.at     = {mapped(cdt,"CdtDeltaX"+k,-0.3f,0.3f,0),mapped(cdt,"CdtDeltaY"+k,-0.3f,0.3f,0.1f),
                   mapped(cdt,p.car==2 ? "CdtDeltaZ_CENTRAL"+k : "CdtDeltaZ"+k,-0.3f,0.3f,0)};
      if(s==1) sp.at.x += side;
      if(s==2) sp.at.x -= side;
      p.body.push_back(sp);
      }
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
