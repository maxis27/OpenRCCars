#include "race.h"

#include <cmath>

using namespace rc;

Race::Race(Checkpoints& route, int laps) : route(route), lapCount(laps) {
  }

void Race::restart(const Mat4& start) {
  route.reset(start);
  state    = Phase::Countdown;
  clock    = 0;
  lapStart = 0;
  times.clear();
  wrong    = false;
  awayTime = 0;
  backTime = 0;
  }

float Race::bestLap() const {
  return times.empty() ? 0.f : *std::min_element(times.begin(),times.end());
  }

Race::Event Race::update(float dt, const Vec3& car, const Vec3& velocity) {
  if(state==Phase::Finished)
    return Event::None;

  clock += dt;
  if(state==Phase::Countdown) {
    route.update(car); // follows the car, nothing to pass at the start
    if(clock<CountdownTime)
      return Event::None;
    state = Phase::Racing;
    return Event::Go;
    }

  // wrong way: moving away from the point the arrow shows for 1.5 s, cleared once heading for it again
  if(!route.empty()) {
    Vec3 v  = velocity;
    Vec3 to = route.route()[route.guide()].position-car;
    v.y = to.y = 0;
    const float speed = v.length(), dist = to.length();
    if(speed>1.f && dist>1e-3f) {
      const float c = Vec3::dot(v,to)/(speed*dist);
      if(c<-0.3f) {
        awayTime += dt;
        backTime  = 0;
        }
      else if(c>0.3f) {
        backTime += dt;
        awayTime  = 0;
        }
      }
    if(awayTime>1.5f)
      wrong = true;
    if(backTime>0.3f)
      wrong = false;
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
    state = Phase::Finished;
    wrong = false;
    return Event::Finish;
    }
  return best ? Event::BestLap : Event::Lap;
  }
