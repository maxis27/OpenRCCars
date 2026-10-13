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
// The computer's cars have one positioned sound, carAI_motor (motorAI_accel1 in a loop above 2 km/h, gone 7 m away).
class CarSound {
  public:
    struct Input {
      float speed    = 0;     // km/h, whichever way
      bool  gas      = false; // forward key
      bool  back     = false; // brake / reverse key
      bool  braking  = false; // the back key while still rolling forward
      bool  airborne = false; // no wheel on the ground
      bool  racing   = false; // the race is on (not the countdown, not after the finish)
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
    // speed band for the start points
    static int band(float kmh);

    BankChannel motor;
    BankChannel decel;
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
    void update(float speedKmh, bool racing, float distance, float side);
    void stop();

  private:
    BankChannel motor;
  };

}
