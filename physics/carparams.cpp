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

}

CarParams CarParams::load(PackFile& pack, int32_t car, int32_t upgrade) {
  static const char* const UpgradeKey[] = {"OverkillUpgrades","BuggyUpgrades","HummerUpgrades"};

  CarParams p;
  p.car     = std::clamp(car,1,3);
  p.upgrade = std::clamp(upgrade,0,3);
  const std::string n = std::to_string(p.car);

  if(auto e = pack.find("Splines\\splAccelBase"+n+".bspl"))
    p.accel = Spline::parse(pack.read(*e));
  if(p.accel.empty())
    p.accel = Spline({{0,8.f},{25.f,0}});
  static const char* const BoostSpline[] = {"OVERKILL","BUGGY","HUMMER"};
  if(auto e = pack.find(std::string("Splines\\")+BoostSpline[p.car-1]+".bspl"))
    p.boostAccel = Spline::parse(pack.read(*e));
  if(p.boostAccel.empty())
    p.boostAccel = p.accel;

  if(auto e = pack.find("Scripts\\upgrades.ini")) {
    auto d  = pack.read(*e);
    auto up = KeySections::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
    auto pick = [&](const char* section) {
      auto& v = up.get(section,UpgradeKey[p.car-1]);
      return size_t(p.upgrade)<v.size() ? v[size_t(p.upgrade)] : 1.f;
      };
    p.accelScale = pick("BOOSTERS");
    p.speedScale = pick("RESONATORS");
    const float tires = pick("TIRES");
    p.gripFront *= tires;
    p.gripRear  *= tires;
    }

  // engine<N>.ini: speedBaseMax (33/38/30) tracks the end of the acceleration spline (25/30/20); the
  // spline's speed axis is read as km/h.
  p.topSpeed = p.accel.points().back().x/3.6f * p.speedScale;

  const auto steer = sliders(pack,"Settings\\steering"+n+".ini");
  p.steerAngle = float(steer.raw("AngleSteer",30));

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

  // tires<N>.ini: CoeffFrontTires / CoeffRearTires 1..10
  const auto tir = sliders(pack,"Settings\\tires"+n+".ini");
  p.gripFront *= float(tir.raw("CoeffFrontTires",7))/7.f;
  p.gripRear  *= float(tir.raw("CoeffRearTires", 7))/7.f;

  // resistance<N>.ini: coeffFrictionBearings 6, coeffAirResistance 33
  const auto res = sliders(pack,"Settings\\resistance"+n+".ini");
  p.rolling = float(res.raw("coeffFrictionBearings",6))*0.05f;
  p.airDrag = float(res.raw("coeffAirResistance",33))*0.0004f;

  // cdt<N>.ini: CenterMassOY 17
  const auto cdt = sliders(pack,"Settings\\cdt"+n+".ini");
  p.comHeight = unit(cdt,"CenterMassOY",0.17f);
  return p;
  }

float CarParams::acceleration(float v, bool boost) const {
  const float kmh = v*3.6f/speedScale;   // RESONATORS stretch the curve to a higher top speed
  const float a   = boost ? std::max(boostAccel.at(kmh),accel.at(kmh)) : accel.at(kmh);
  return std::max(a,0.f)*accelScale;
  }
