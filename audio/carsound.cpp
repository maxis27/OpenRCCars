#include "carsound.h"

#include <cmath>

#include "formats/collision.h"

namespace rc {

// where motor_accel1 and motor_reverse start for each speed band (0x573780, 0x573790; bands 0x5737a0, km/h)
static const float BandSpeed[4]    = {0.f, 10.f, 26.f, 35.f};
static const float AccelStart[4]   = {0.f, 1380.f, 3350.f, 6810.f};
static const float ReverseStart[4] = {0.f, 1000.f, 3000.f, 3000.f};

static const char* const MotorChannel[3]  = {"car_motor",    "car_motor_b",    "car_motor_h"};
static const char* const DecelChannel[3]  = {"car_motor_2",  "car_motor_b_2",  "car_motor_h_2"};
static const char* const SpinChannel[3]   = {"car_motor_ws", "car_motor_b_ws", "car_motor_h_ws"};

// car_Surfaces by SoundSurface (table 0x573620): rolling, hit, touch
static const char* const SurfaceSounds[11][3] = {
  {nullptr,            nullptr,           nullptr},
  {"car_surf_sand",    "car_hit_sand",    nullptr},             // sand
  {"car_surf_wetsand", "car_hit_sand",    nullptr},             // wet sand
  {"car_surf_sand",    "car_hit_sand",    nullptr},             // trampled sand
  {nullptr,            nullptr,           nullptr},             // grass
  {"car_surf_gravel",  "car_hit_gravel",  nullptr},             // gravel
  {"car_surf_asphalt", "car_hit_asphalt", nullptr},             // tin
  {"car_surf_wood",    "car_hit_wood",    "car_touch_wood"},    // wood
  {"car_surf_stone",   "car_hit_stone",   "car_touch_stone"},   // stone
  {"car_surf_metal",   "car_hit_metal",   "car_touch_metal"},   // metal
  {"car_surf_water",   "car_hit_water",   nullptr},             // water
  };

// a contact's volume (0x50a7d0): -5 dB at 1 km/h up to 0 dB at 35 km/h
static float contactDb(float impact) {
  if(impact<1.f)
    return -5.f;
  if(impact>=35.f)
    return 0.f;
  return (impact-1.f)*(5.f/34.f)-5.f;
  }

// rolling (0x50aa7f): -20 dB at 0.5 km/h up to 0 dB at 35 km/h, in water -10 dB up to 0 dB at 15 km/h
static float rollDb(float speed, bool water) {
  const float lo = water ? -10.f : -20.f, top = water ? 15.f : 35.f;
  if(speed<0.5f)
    return lo;
  if(speed>=top)
    return 0.f;
  return (speed-0.5f)*(-lo/(top-0.5f))+lo;
  }

CarSound::CarSound(Mixer& mixer, SoundLibrary& lib, int car) {
  if(car<0 || car>2)
    car = 0;
  motor      = BankChannel(mixer,lib,MotorChannel[car]);
  decel      = BankChannel(mixer,lib,DecelChannel[car]);
  suspension = BankChannel(mixer,lib,"car_suspension");
  cdt        = BankChannel(mixer,lib,"car_CDT&Boost");
  surfaces   = BankChannel(mixer,lib,"car_Surfaces");
  spin       = BankChannel(mixer,lib,SpinChannel[car]);
  for(int i=0; i<11; ++i)
    for(int k=0; k<3; ++k)
      surf[i][k] = SurfaceSounds[i][k] ? surfaces.find(SurfaceSounds[i][k]) : -1;
  }

int CarSound::band(float kmh) {
  for(int i=3; i>0; --i)
    if(kmh>BandSpeed[i])
      return i;
  return 0;
  }

void CarSound::stop() {
  motor.stop(true);
  decel.stop(true);
  suspension.stop(true);
  cdt.stop(true);
  surfaces.stop(true);
  spin.stop(true);
  prevState   = State::Idle;
  wasAirborne = false;
  quiet       = 0;
  spinTime    = 0;
  spinning    = false;
  st         = State::Idle;
  on         = false;
  changed    = false;
  idleTime   = 0;
  sinceStart = 1e3f;
  }

void CarSound::update(const Input& in, float dt) {
  // --- state (0x50acf0) ---
  speed = in.speed;
  const State prev     = st;
  const bool  pressing = in.gas || in.back;
  const bool  wasAccel = (prev==State::Start || prev==State::Accel);
  sinceStart += dt;
  if(speed<1.f) {
    // standing: a key held keeps the state until the car moves
    if(!pressing || speed>0.3f)
      st = State::Idle;
    }
  else if(pressing && !in.braking && !in.airborne) {
    if(wasAccel)
      st = State::Accel;
    else if(sinceStart>0.33f) {
      st = State::Start;
      sinceStart = 0;
      }
    }
  else {
    if(speed<2.f)
      st = State::Idle;
    else if(!wasAccel)
      st = State::Coast;
    else
      st = State::Release;
    }

  // the engine starts with the first move and stops after 4 s standing
  changed = false;
  if(on) {
    if(st==State::Idle) {
      idleTime += dt;
      if(idleTime>4.f) {
        on      = false;
        changed = true;
        }
      }
    else
      idleTime = 0;
    }
  else {
    idleTime = 0;
    if(st!=State::Idle && st!=State::Coast) {
      on      = true;
      changed = true;
      }
    }

  // --- what happened to the body (0x50acf0) ---
  flyingEvt  = in.airborne && !wasAirborne;
  landingEvt = !in.airborne && wasAirborne && in.ground!=SoundSurface::Water;
  event      = Contact{};
  if(quiet>1e-6f)
    quiet -= dt;
  else {
    if(in.speed>0)
      event = in.contact;
    // landing on something with a sound is a hit of 17.5 km/h
    if(wasAirborne && !in.airborne && in.ground!=SoundSurface::None && event.surface==SoundSurface::None)
      event = Contact{in.ground,true,17.5f};
    }
  if(event.surface!=SoundSurface::None)
    quiet = 1.f;
  cdtSurf     = event.surface!=SoundSurface::None && event.hit && event.impact>20.f;
  wasAirborne = in.airborne;

  // wheelspin: gas held below 5 km/h, not braking, for 0.3 s. Our wheels never slip (btRaycastVehicle), so instead of
  // RCCars.exe's slip of the rear wheels (0x5019a0) this takes the car not gaining speed while the wheels are down.
  const float gain = dt>0 ? (in.speed-lastSpeed)/dt : 0.f;  // km/h per s
  lastSpeed = in.speed;
  if(!in.braking && in.speed<5.f && !in.airborne && (in.gas || in.back) && gain<3.f) {
    spinTime += dt;
    if(spinTime>0.3f)
      spinning = true;
    }
  else {
    spinTime = 0;
    spinning = false;
    }

  // the channels in RCCars.exe's order (0x50a0f8)
  float accelPos = 0;
  motorSound(in,accelPos);
  surfaceSound(in);
  cdtSound(in,accelPos);
  suspensionSound(in);
  decelSound(in,accelPos);
  wheelspinSound();
  prevState = st;
  }

// car_motor (0x50a2a0)
void CarSound::motorSound(const Input& in, float& accelPos) {
  motor.setGain(in.inWater ? -5.f : 0.f);
  if(!on) {
    if(changed)
      motor.play(Off);
    return;
    }
  if(!in.racing) {
    motor.play(IdleSnd);
    return;
    }
  if(!in.airborne) {
    if(motor.isPlaying(Flying)) {
      if(st==State::Accel) {
        // back on the ground speeding up: the next start fits the speed
        const int b = band(speed);
        motor.setTail(Accel1,0);
        motor.setStart(Accel1,AccelStart[b]);
        motor.setTail(Reverse,0);
        motor.setStart(Reverse,ReverseStart[b]);
        motor.setTail(Engine,0);
        }
      motor.play(FlyingEnd);
      return;
      }
    if(motor.isPlaying(FlyingEnd))
      return;
    }
  else if(motor.isPlaying(Flying))
    return;

  if(motor.isPlaying(Accel1))
    accelPos = motor.position();
  switch(st) {
    case State::Start: {
      const int b = band(speed);
      motor.setTail(Accel1,0);
      motor.setStart(Accel1,AccelStart[b]);
      motor.setTail(Reverse,0);
      motor.setStart(Reverse,ReverseStart[b]);
      motor.setTail(Engine,0);
      motor.play(in.back ? Reverse : Accel1);
      return;
      }
    case State::Accel:
      if(in.airborne && speed>20.f)
        motor.play(Flying);
      else
        motor.play(Engine);    // waits for the acceleration to end
      return;
    case State::Release:
      // what plays goes on for a second, fading, then the idle
      motor.setTail(Accel1,1000);
      motor.setTail(Reverse,1000);
      motor.setTail(Engine,1000);
      motor.play(IdleSnd);
      return;
    case State::Idle:
    case State::Coast:
      motor.play(IdleSnd);
      return;
    }
  }

// car_motor_2: decel1..3 (0x50a4e0)
void CarSound::decelSound(const Input& in, float accelPos) {
  decel.setGain(in.inWater ? -5.f : 0.f);
  if(!in.racing) {
    decel.stop();
    return;
    }
  if(speed<3.f)
    decel.stop();
  if(st==State::Release) {
    if(speed<5.f)
      decel.play(2);
    else
      decel.play(accelPos<AccelStart[1] ? 0 : 1);  // a short push or a longer one (0x50b3d0)
    }
  else if(st!=State::Coast)
    decel.stop();
  }

// car_Surfaces (0x50a7d0)
void CarSound::surfaceSound(const Input& in) {
  if(!in.racing) {
    surfaces.stop();
    return;
    }
  if(event.surface<11 && in.speed>1.f) {
    // a hit, or a touch
    const int s = event.hit ? (event.impact>1.f ? surf[event.surface][1] : -1)
                            : (event.impact>1e-6f ? surf[event.surface][2] : -1);
    if(s>=0) {
      surfaces.setVolume(s,contactDb(event.impact));
      surfaces.play(s);
      return;
      }
    }
  // a hit or a touch plays out before the wheels are heard again
  for(auto& t:surf)
    if((t[1]>=0 && surfaces.isPlaying(t[1])) || (t[2]>=0 && surfaces.isPlaying(t[2])))
      return;
  const int roll = (in.ground<11 && !in.airborne && in.speed>=2.f) ? surf[in.ground][0] : -1;
  if(roll<0) {
    surfaces.stop();
    return;
    }
  surfaces.setVolume(roll,rollDb(in.speed,in.ground==SoundSurface::Water));
  surfaces.play(roll);
  }

// car_CDT&Boost (0x50a620)
void CarSound::cdtSound(const Input& in, float accelPos) {
  if(!in.racing) {
    cdt.stop();
    return;
    }
  if(in.shot) {
    cdt.play(CdtBullet);
    return;
    }
  if(in.carBump) {
    cdt.play(CdtCar);
    return;
    }
  if(in.objBump) {
    cdt.play(CdtObj);
    return;
    }
  if(cdtSurf) {
    cdt.play(CdtSurf);
    return;
    }
  // the turbo while speeding up (0x50adb4)
  const bool boosting = in.boost && in.speed>0.5f && (prevState==State::Start || prevState==State::Accel);
  if(boosting) {
    if(!cdt.isPlaying() || cdt.isPlaying(BoostEnd)) {
      cdt.setStart(Boost,accelPos);
      cdt.play(Boost);
      return;
      }
    }
  else if(cdt.isPlaying(Boost)) {
    cdt.play(BoostEnd);
    return;
    }
  if(in.braking && in.speed>1.f)
    cdt.play(Break);
  else if(cdt.isPlaying(Break))
    cdt.stop();
  }

// car_suspension (0x50a750)
void CarSound::suspensionSound(const Input& in) {
  if(!in.racing) {
    suspension.stop();
    return;
    }
  if(flyingEvt)
    suspension.play(in.jump ? CarFlying : CarFlying2);
  else if(landingEvt)
    suspension.play(CarLanding);
  }

// car_motor_ws (0x50ac80)
void CarSound::wheelspinSound() {
  const bool on = spin.isPlaying(AccelWs) || spin.isPlaying(EngineWs);
  if(spinning) {
    if(!on) {
      spin.play(AccelWs);
      spin.play(EngineWs);   // queued after it
      }
    }
  else if(on)
    spin.play(DecelWs);
  }

AiCarSound::AiCarSound(Mixer& mixer, SoundLibrary& lib)
  : motor(mixer,lib,"carAI_motor"), surfaces(mixer,lib,"carAI_Surfaces") {
  }

void AiCarSound::update(float speedKmh, bool racing, float distance, float side, bool inWater) {
  motor.setPlace(distance,side);
  surfaces.setPlace(distance,side);
  if(!racing || speedKmh<2.f)
    motor.stop();
  else
    motor.play(0);
  // carAI_Surfaces (table 0x5736d0): only water rolls
  const int water = surfaces.find("carAI_surf_water");
  if(racing && inWater && speedKmh>=2.f && water>=0) {
    surfaces.setVolume(water,rollDb(speedKmh,true));
    surfaces.play(water);
    }
  else if(surfaces.isPlaying(water))
    surfaces.stop();
  }

void AiCarSound::stop() {
  motor.stop(true);
  surfaces.stop(true);
  }

}
