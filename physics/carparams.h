#pragma once

#include <cstdint>
#include <string>

#include "formats/pack.h"
#include "formats/settings.h"

namespace rc {

// Driving parameters of one car (1 = Overkill, 2 = Buggy, 3 = Hummer), from RCCars.pack:
//   Settings\engine<N>.ini, springs<N>.ini, tires<N>.ini, steering<N>.ini, resistance<N>.ini, cdt<N>.ini,
//   Splines\splAccelBase<N>.bspl, Splines\OVERKILL|BUGGY|HUMMER.bspl (boost), Scripts\upgrades.ini and
//   Settings\Water.ini (shared by all cars).
// The tuning sliders (1..100) have no units in the data. How a slider becomes a physical value is our guess,
// kept in one place (CarParams::load) so it can be tuned against the original game.
struct CarParams {
  int32_t car     = 1;
  int32_t upgrade = 0;          // 0..3: column of Scripts\upgrades.ini (0 = stock)

  float   mass        = 1.5f;   // kg
  float   topSpeed    = 7.f;    // m/s, end of the acceleration spline
  Spline  accel;                // m/s^2 over speed in km/h (splAccelBase<N>.bspl)
  Spline  boostAccel;           // the same while boosting (Shift): OVERKILL/BUGGY/HUMMER.bspl, top 32/38/30 km/h
  float   boostTime   = 3.f;    // s of boost in a full turbo tank (the manual: units charged on the track)
  float   boostRecharge = 15.f; // s to refill an empty tank while driving
  float   accelScale  = 1.f;    // BOOSTERS upgrade
  float   speedScale  = 1.f;    // RESONATORS upgrade
  float   reverseSpeed = 0.35f; // fraction of top speed
  float   brakeDecel  = 12.f;   // m/s^2

  float   jumpSpeed   = 2.4f;   // m/s upwards; RCCars.exe has a "Jump" action but no setting for it (~0.3 m high)
  float   steerAngle  = 30.f;   // degrees, steering<N>.ini AngleSteer
  float   steerAtTop  = 0.4f;   // fraction of steerAngle left at top speed

  float   suspStiffness   = 250.f; // 1/s^2 (per unit of mass, like btRaycastVehicle)
  float   suspDamping     = 6.f;   // 1/s, rebound
  float   suspCompression = 4.f;   // 1/s
  float   suspRest        = 0.04f; // m
  float   suspTravel      = 0.03f; // m

  float   gripFront   = 1.5f;   // btWheelInfo::m_frictionSlip
  float   gripRear    = 1.5f;
  float   rollInfluence = 0.03f;

  float   rolling     = 0.3f;   // m/s^2, bearings friction while coasting
  float   airDrag     = 0.013f; // 1/m: a = airDrag * v^2
  // surfaces (see rc::Surface): deep sand and water hold the car back with a force proportional to its speed
  float   deepSandDrag = 0.7f;  // 1/s, tires<N>.ini coeffDeepSand 7, all wheels in deep sand
  float   waterDrag    = 2.f;   // 1/s, tires<N>.ini coeffWater 20 (Hummer 7), all wheels in water
  float   waterDropWheels = 0.15f; // fraction of speed lost when the first wheel splashes in, Water.ini speedDropOther
  float   waterDropBody   = 0.25f; // ... when the body does, Water.ini speedDropCenter
  float   waterDamping    = 0.95f; // velocity kept per 1/30 s with the body under water, Water.ini DampingVelocity2
  float   drownTime       = 1.f;   // s fully under water before the car counts as drowned

  float   comHeight   = 0.17f;  // centre of mass, fraction of the car's height above its lowest point
  float   inertiaRoll  = 1.f;   // multipliers of the box inertia
  float   inertiaPitch = 1.f;

  static CarParams load(PackFile& pack, int32_t car, int32_t upgrade = 0);

  // engine acceleration at forward speed `v` (m/s), upgrades included
  float   acceleration(float v, bool boost = false) const;
  };

}
