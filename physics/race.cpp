#include "race.h"

#include <cmath>

using namespace rc;

Race::Race(Checkpoints& route, int laps) : route(route), lapCount(laps) {
  }

void Race::restart(const Mat4& start, bool again) {
  route.reset(start);
  state    = Phase::Countdown;
  clock    = 0;
  startAt  = (again ? 0.f : EntryDelay)+CountTime;
  lapStart = 0;
  finishAt = 0;
  times.clear();
  wrong     = false;
  wrongTime = 0;
  }

float Race::bestLap() const {
  return times.empty() ? 0.f : *std::min_element(times.begin(),times.end());
  }

Race::Event Race::update(float dt, const Vec3& car, const Vec3& heading) {
  if(state==Phase::Finished) {
    clock += dt;
    return Event::None;
    }

  const float was = clock;
  clock += dt;
  if(state==Phase::Countdown) {
    route.update(car); // follows the car, nothing to pass at the start
    if(clock<startAt) {
      // "3" (Show3): the original plays the start sounds with it
      const float count = startAt-CountTime;
      return (was<count || (was==0 && count==0)) && clock>=count ? Event::Count : Event::None;
      }
    state = Phase::Racing;
    return Event::Go;
    }

  // wrong way (RCCars.exe 0x4e8f00): the car heading against the way on (Checkpoints::wayOn) runs a timer up, by a
  // second a second to at most WrongMax, heading along it runs the timer down three times as fast; past WrongAfter
  // it is the wrong way, but not within WrongNear of the last main checkpoint passed (horizontally)
  if(!route.empty()) {
    const Vec3 to = route.wayOn(car);
    if(Vec3::dot(heading,to)>=0) {
      if(wrongTime>0)
        wrongTime -= 3.f*dt;
      }
    else if(wrongTime<WrongMax)
      wrongTime += dt;
    wrong = wrongTime>WrongAfter;
    if(wrong) {
      Vec3 d = route.route()[route.lastPassed()].position-car;
      d.y = 0;
      wrong = d.length()>=WrongNear;
      }
    }

  if(!route.update(car))
    return Event::None;
  if(route.laps()<=times.size())
    return Event::Checkpoint;

  // back on cp_1: a lap done
  const float lap  = lapTime();
  const bool  best = !times.empty() && lap<bestLap();
  times.push_back(lap);
  lapStart = time();
  if(int(times.size())>=lapCount) {
    finishAt = time();
    state    = Phase::Finished;
    wrong = false;
    return Event::Finish;
    }
  return best ? Event::BestLap : Event::Lap;
  }
