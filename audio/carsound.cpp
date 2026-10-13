#include "carsound.h"

namespace rc {

// where motor_accel1 and motor_reverse start for each speed band (0x573780, 0x573790; bands 0x5737a0, km/h)
static const float BandSpeed[4]    = {0.f, 10.f, 26.f, 35.f};
static const float AccelStart[4]   = {0.f, 1380.f, 3350.f, 6810.f};
static const float ReverseStart[4] = {0.f, 1000.f, 3000.f, 3000.f};

static const char* const MotorChannel[3]  = {"car_motor",   "car_motor_b",   "car_motor_h"};
static const char* const DecelChannel[3]  = {"car_motor_2", "car_motor_b_2", "car_motor_h_2"};

CarSound::CarSound(Mixer& mixer, SoundLibrary& lib, int car) {
  if(car<0 || car>2)
    car = 0;
  motor = BankChannel(mixer,lib,MotorChannel[car]);
  decel = BankChannel(mixer,lib,DecelChannel[car]);
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

  // --- car_motor (0x50a2a0) ---
  float accelPos = 0;
  [&]() {
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
    }();

  // --- car_motor_2: decel1..3 (0x50a4e0) ---
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

AiCarSound::AiCarSound(Mixer& mixer, SoundLibrary& lib)
  : motor(mixer,lib,"carAI_motor") {
  }

void AiCarSound::update(float speedKmh, bool racing, float distance, float side) {
  motor.setPlace(distance,side);
  if(!racing || speedKmh<2.f)
    motor.stop();
  else
    motor.play(0);
  }

void AiCarSound::stop() {
  motor.stop(true);
  }

}
