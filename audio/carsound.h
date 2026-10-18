#pragma once

#include "soundlib.h"

namespace rc {

// The engine of a car as RCCars.exe plays it (state 0x50acf0, channels 0x50a2a0 / 0x50a4e0 / 0x50a5b0). The pitch never
// changes: motor_accel1.wav is a 28 s recording of the car speeding up, and a push on the gas starts it at the point
// that matches the speed (0, 1.38, 3.35 or 6.81 s from 0, 10, 26 or 35 km/h). When it has played out the motor_engine
// loop follows; letting go plays one more second of it fading out, then motor_idle, with motor_decel* on a second
// channel. motor_reverse works the same way backwards, motor_flying2 is the engine in the air above 20 km/h and
// motor_off ends it after 4 s standing. The engine stays silent until the car first moves. Overkill, Buggy and Hummer
// have their own recordings (channels car_motor, car_motor_b, car_motor_h).
//
// Around the engine (0x50a620 / 0x50a750 / 0x50a7d0 / 0x50ac80, state 0x50acf0):
//  - car_CDT&Boost: car_break while braking, car_boost from where motor_accel1 is while the turbo works and the car
//    speeds up (car_boost_end after it), car_cdt_bullet hit by a bullet, car_cdt_car bumping into another car,
//    car_cdt_obj running into a person, guard, dog or road car, car_cdt_surf a hit harder than 20 km/h (in that
//    order when several come at once);
//  - car_suspension: car_flying (jump key held) or car_flying2 taking off, car_landing back on the ground;
//  - car_Surfaces: the wheels rolling on sand, gravel, wood, stone, metal or water from 2 km/h (-20 dB at 0.5 km/h
//    up to 0 dB at 35 km/h, in water -10 dB up to 0 dB at 15 km/h), and a hit or a touch of the body against the
//    ground or a prop (-5 dB at 1 km/h up to 0 dB at 35 km/h along the contact normal), at most one a second;
//    landing on a surface with sounds counts as a hit of 17.5 km/h;
//  - car_motor_ws: wheelspin, gas held below 5 km/h with the wheels spinning for 0.3 s: motor_accel1_ws, then the
//    motor_engine_ws loop, motor_decel1_ws when it stops;
//  - the engine is 5 dB quieter with the car in water.
//
// The computer's cars have one positioned sound, carAI_motor (motorAI_accel1 in a loop above 2 km/h, gone 7 m away),
// and carAI_Surfaces, of which our opponents (recordings, no contacts) only use carAI_surf_water in water.
class CarSound {
  public:
    // what the car's body touched (Vehicle::Contact)
    struct Contact {
      uint8_t surface = 0;     // SoundSurface, 0 = nothing
      bool    hit     = false;
      float   impact  = 0;     // km/h along the contact normal
      };
    struct Input {
      float   speed    = 0;     // km/h, whichever way
      bool    gas      = false; // forward key
      bool    back     = false; // brake / reverse key
      bool    braking  = false; // the back key while still rolling forward
      bool    airborne = false; // no wheel on the ground
      bool    racing   = false; // the race is on (not the countdown, not after the finish)
      bool    boost    = false; // the turbo works
      bool    jump     = false; // jump key held
      bool    inWater  = false; // the body is in water
      uint8_t ground   = 0;     // SoundSurface under the wheels (Vehicle::groundSound)
      Contact contact;          // the body's contact in this update
      bool    carBump  = false; // started touching another car
      bool    objBump  = false; // ran into a person, guard, dog or road car (Characters/Traffic::bumpedPlayer)
      bool    shot     = false; // a bullet hit the car
      };

    // `car` 0..2: Overkill, Buggy, Hummer
    CarSound(Mixer& mixer, SoundLibrary& lib, int car);

    void update(const Input& in, float dt);
    void stop();

    enum class State { Idle, Start, Accel, Release, Coast };
    State state()   const { return st; }
    bool  running() const { return on; }

  private:
    // sounds of car_motor*
    enum { Accel1, Engine, IdleSnd, Off, Flying, FlyingEnd, Reverse };
    // sounds of car_CDT&Boost, car_suspension, car_motor_ws
    enum { CdtCar, CdtObj, CdtSurf, Break, Boost, BoostEnd, CdtBullet };
    enum { CarFlying, CarLanding, CarFlying2 };
    enum { AccelWs, EngineWs, DecelWs };
    // speed band for the start points
    static int band(float kmh);

    void motorSound(const Input& in, float& accelPos);
    void decelSound(const Input& in, float accelPos);
    void surfaceSound(const Input& in);
    void cdtSound(const Input& in, float accelPos);
    void suspensionSound(const Input& in);
    void wheelspinSound();

    BankChannel motor;
    BankChannel decel;
    BankChannel suspension;
    BankChannel cdt;
    BankChannel surfaces;
    BankChannel spin;
    // car_Surfaces sounds by SoundSurface: rolling, hit, touch (-1 = none)
    int         surf[11][3];
    State       prevState  = State::Idle;
    bool        wasAirborne = false;
    bool        flyingEvt  = false;
    bool        landingEvt = false;
    bool        cdtSurf    = false;
    Contact     event;            // contact of this update that makes a sound
    float       quiet      = 0;   // s until the next contact sound
    float       spinTime   = 0;
    float       lastSpeed  = 0;
    bool        spinning   = false;
    float       speed    = 0;
    State       st       = State::Idle;
    float       sinceStart = 1e3f;  // s since the last State::Start
    bool        on       = false;   // the engine has been started (and not switched off)
    bool        changed  = false;   // `on` changed in this update
    float       idleTime = 0;
  };

class AiCarSound {
  public:
    AiCarSound(Mixer& mixer, SoundLibrary& lib);

    // `distance` from the listener, `side` -1 (left) .. 1 (right)
    void update(float speedKmh, bool racing, float distance, float side, bool inWater = false);
    void stop();

  private:
    BankChannel motor;
    BankChannel surfaces;
  };

}
