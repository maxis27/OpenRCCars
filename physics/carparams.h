#pragma once

#include <cstdint>
#include <string>

#include "formats/pack.h"
#include "formats/settings.h"

namespace rc {

// Driving parameters of one car (1 = Overkill, 2 = Buggy, 3 = Hummer), from RCCars.pack:
//   Settings\engine<N>.ini, springs<N>.ini, tires<N>.ini, steering<N>.ini, resistance<N>.ini, cdt<N>.ini,
//   Splines\splAccelBase<N>.bspl and Scripts\upgrades.ini.
// The tuning sliders (1..100) have no units in the data. How a slider becomes a physical value is our guess,
// kept in one place (CarParams::load) so it can be tuned against the original game.
struct CarParams {
  int32_t car     = 1;
  int32_t upgrade = 0;          // 0..3: column of Scripts\upgrades.ini (0 = stock)

  float   mass        = 1.5f;   // kg
  float   topSpeed    = 7.f;    // m/s, end of the acceleration spline
  Spline  accel;                // m/s^2 over speed in km/h (splAccelBase<N>.bspl)
  float   accelScale  = 1.f;    // BOOSTERS upgrade
  float   speedScale  = 1.f;    // RESONATORS upgrade
  float   reverseSpeed = 0.35f; // fraction of top speed
  float   brakeDecel  = 12.f;   // m/s^2

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
  float   comHeight   = 0.17f;  // centre of mass, fraction of the car's height above its lowest point
  float   inertiaRoll  = 1.f;   // multipliers of the box inertia
  float   inertiaPitch = 1.f;

  static CarParams load(PackFile& pack, int32_t car, int32_t upgrade = 0);

  // engine acceleration at forward speed `v` (m/s), upgrades included
  float   acceleration(float v) const;
  };

}
