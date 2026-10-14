#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "formats/math3d.h"
#include "formats/pack.h"
#include "formats/settings.h"

namespace rc {

// Driving parameters of one car (1 = Overkill, 2 = Buggy, 3 = Hummer), from RCCars.pack:
//   Settings\engine<N>.ini, springs<N>.ini, tires<N>.ini, steering<N>.ini, resistance<N>.ini, cdt<N>.ini,
//   Splines\OVERKILL|BUGGY|HUMMER.bspl (engine), Splines\steering<N>.bspl, Scripts\upgrades.ini and
//   Settings\Water.ini (shared by all cars).
// RCCars.exe maps every slider linearly from its position 1..100 (whatever range the .ini file shows) onto a
// fixed range of its own; the values marked "RCCars.exe" below use the game's mapping (see README, "Fizyka
// w RCCars.exe"). The rest is our guess. The original car weighs 1 kg and falls at 10 m/s^2, so its forces are
// accelerations.
struct CarParams {
  int32_t car     = 1;
  int32_t upgrade = 0;          // 0..3: column of Scripts\upgrades.ini (0 = stock)

  float   mass        = 1.f;    // kg (RCCars.exe)

  // engine (RCCars.exe): `accel` is the acceleration in m/s^2 over the forward speed in km/h divided by
  // (throttle * resonator * boost ratio); the engine also cancels the air and bearings resistance while it pulls
  Spline  accel;                // Splines\OVERKILL|BUGGY|HUMMER.bspl (splAccelBase<N>.bspl is not used by the game)
  float   topSpeed      = 9.f;  // m/s, engine<N>.ini speedBaseMax (km/h) * RESONATORS: no engine force above it
  float   boostTopSpeed = 10.f; // m/s, speedBoostMax (km/h) * RESONATORS
  float   boostAccel    = 1.2f; // the curve times this while boosting
  float   resonator   = 1.f;    // RESONATORS upgrade: stretches the curve and the top speeds
  float   accelScale  = 1.f;    // RESONATORS *_SPL_OY upgrade: scales the curve
  float   reverseAccel      = 4.f;    // m/s^2 backwards (6 while boosting) ...
  float   reverseFullSpeed  = 2.8333f; // ... up to this speed, m/s, then less ...
  float   reverseSpeed      = 3.3333f; // ... down to none at this one (12 km/h)

  // turbo tank (RCCars.exe), in units: it starts full, refills by itself, gains BoostPerCheckpoint for every
  // checkpoint and drains while the boost key is held
  float   boostTank   = 50.f;   // units in a full tank: 50..100, from the refill rate compared to the other cars'
  float   boostCharge = 1.f;    // units/s: engine<N>.ini boostSpeedUp * BOOSTERS upgrade * 0.95
  float   boostDrain  = 5.f;    // units/s: engine<N>.ini boostSpeedDown
  static constexpr float BoostPerCheckpoint = 10.f;
  static constexpr float BoostLockTime      = 0.9f;  // s after running dry before the boost works again ...
  static constexpr float BoostRestart       = 10.f;  // ... with at least this many units in the tank

  float   jumpSpeed   = 3.f;    // m/s along the car's up axis (Buggy 3.3), RCCars.exe
  static constexpr float JumpCooldown = 0.5f;
  static constexpr float JumpFlipAngle = 80.f; // degrees from upright: Jump puts the car back on its wheels instead

  // steering (RCCars.exe): a steering position moves at SteerRate(v) towards the side held (SteerBack towards the
  // middle); steering<N>.bspl turns it into the wheels' angle, which stops growing at AngleSteer
  float   steerAngle  = 29.3f;  // degrees, steering<N>.ini AngleSteer
  Spline  steerCurve;           // degrees over the steering position
  static constexpr float SteerBack = 6.f;
  // positions per second while turning in: 4.5 when standing, 0.5 less per m/s, 1 from 7 m/s on
  static float steerRate(float speed);

  // suspension (RCCars.exe, springs<N>.ini, 0x4f91b0): every wheel is a sphere hanging from a point above its hub
  // on a spring along the car's up axis. The spring pushes only while it is shorter than its length, with
  // k * (springLength - length) - springDamping * (its rate of change), and is as stiff as it must be for the
  // springs together to carry the car squeezed by springSag
  float   springLength   = 0.218f; // m: length (0.01..0.5 m)
  float   springMin      = 0.022f; // m: lenMin, the shortest (springLength * 0.1..0.9)
  float   springMax      = 0.240f; // m: lenMax, the longest (springLength * 1.1..2)
  float   springSag      = 0.119f; // m: kPos, how much the springs give under the car standing still
  float   springDamping  = 4.18f;  // N s/m: kSpeed (1..10)
  Vec3    springMount    = {0,0.101f,0}; // dx, touchPoint, dz (0..1 m): the top of the spring from the hub
  // the turning moments of the springs about the car's own axes are multiplied by these, the one about its up
  // axis is dropped
  float   springPitch    = 1.05f;  // coeffMomentOX (0.5..2)
  float   springRoll     = 1.87f;  // coeffMomentOZ (1..3)

  // tires (RCCars.exe, tires<N>.ini, 0x4f7fa0): grip as the sideways acceleration the tires can stop, 5 x grip at
  // the front and 10 x grip at the rear (see Vehicle::tires), times the TIRES upgrade
  float   gripFront   = 0.7f;   // CoeffFrontTires (0.1..1)
  float   gripRear    = 0.7f;   // CoeffRearTires
  // braking with the wheels turned further than driftAngle lets the rear slide: its grip times driftGrip
  float   driftAngle  = 10.f;   // degrees, angleDriftOn (1..45)
  float   driftGrip   = 0.56f;  // coeffDriftRear (1..0.1)

  // collision spheres (RCCars.exe, cdt<N>.ini, 0x4f8400), model space
  float   wheelRadius     = 0.071f; // CdtRadWheel (0.01..0.1 m)
  float   rearWheelRadius = 0.071f; // the same times coeffRadBackWheels (1..2)
  struct Sphere { Vec3 at; float radius = 0; };
  std::vector<Sphere> body;         // CdtRadUp<i> / CdtDeltaX..Z<i>, i = 1..3, each also shifted by
                                    // shiftRoofBaseLefter to the left and to the right
  float   comHeight   = 0.032f; // m above the model's origin: CenterMassOY (0..0.2 m)

  // resistance (RCCars.exe): a = airDrag * v^2 + rolling against the motion
  float   rolling     = 0.5f;   // m/s^2, resistance<N>.ini coeffFrictionBearings
  float   airDrag     = 0.03f;  // 1/m, coeffAirResistance
  // surfaces (RCCars.exe): every wheel in deep sand, and every wheel in water, adds a = k * v^2 against the motion
  // at the wheel; in water k grows from 0 with the wheel 30% under water to all of it at 90%
  float   deepSandDrag = 0.06f; // 1/m per wheel, tires<N>.ini coeffDeepSand
  float   waterDrag    = 0.19f; // 1/m per wheel, coeffWater
  float   waterDropWheels = 0.15f; // fraction of speed lost when the first wheel splashes in, Water.ini speedDropOther
  float   waterDropBody   = 0.25f; // ... when the body does, Water.ini speedDropCenter
  float   waterDamping    = 0.95f; // velocity kept per 1/30 s with the body under water, Water.ini DampingVelocity2
  // RCCars.exe 0x508a10: drowning is timed at a point 0.2 m above the car's origin. Under the water surface the
  // timer runs, 4x as fast when the point is more than 0.5 m under; out of the water it starts again from zero.
  float   drownHeight     = 0.2f;  // m above the car's origin
  float   drownDeep       = 0.5f;  // m under the surface where the timer runs 4x as fast
  float   drownTime       = 2.f;   // s on the timer before the car counts as drowned

  static CarParams load(PackFile& pack, int32_t car, int32_t upgrade = 0);

  // engine acceleration at forward speed `v` (m/s) with the throttle 0..1, upgrades included, resistance not
  float   acceleration(float v, float throttle = 1.f, bool boost = false) const;
  };

}
