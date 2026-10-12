#pragma once

#include <algorithm>
#include <vector>

#include "checkpoints.h"

namespace rc {

// One race of the player alone (the original's quick race, without opponents so far): the countdown with the car
// held at the start, laps through the checkpoints in order, the total and lap times, the best lap, the wrong way
// warning and the finish. Plain logic, no rendering: the game and `rcinfo race` drive it the same way.
class Race {
  public:
    enum class Phase { Countdown, Racing, Finished };

    // what happened during an update(), for the messages on the screen
    enum class Event { None, Go, Checkpoint, Lap, BestLap, Finish };

    // "3", "2", "1" a second each, then "Jazda!" (Language/*/msg_321_s_f.csi)
    static constexpr float CountdownTime = 3.f;
    // the original offers 3, 5 or 7 laps (manual: "Liczba okrążeń")
    static constexpr int   DefaultLaps   = 3;

    Race(Checkpoints& route, int laps = DefaultLaps);

    // from the countdown again, the car standing at `start`
    void   restart(const Mat4& start);
    // `dt` seconds of game time with the car at `car` moving at `velocity` (m/s)
    Event  update(float dt, const Vec3& car, const Vec3& velocity);

    Phase  phase()     const { return state; }
    // the car must stand still: before the start
    bool   holdCar()   const { return state==Phase::Countdown; }
    // seconds until the start, CountdownTime .. 0
    float  countdown() const { return std::max(0.f,CountdownTime-clock); }
    // seconds since the start (stops at the finish)
    float  time()      const { return std::max(0.f,clock-CountdownTime); }
    // seconds into the current lap
    float  lapTime()   const { return time()-lapStart; }
    // the lap being driven, 1 .. laps(); laps() after the finish
    int    lap()       const { return std::min(int(times.size())+1,lapCount); }
    int    laps()      const { return lapCount; }
    void   setLaps(int n)    { lapCount = n; }
    // times of the finished laps
    const std::vector<float>& lapTimes() const { return times; }
    // the fastest finished lap, 0 if none yet
    float  bestLap()   const;
    // driving away from the next checkpoint for a while (missed it, or turned round)
    bool   wrongWay()  const { return wrong; }

    const Checkpoints& checkpoints() const { return route; }

  private:
    Checkpoints&       route;
    Phase              state    = Phase::Countdown;
    int                lapCount = DefaultLaps;
    float              clock    = 0;   // since restart(), the countdown included
    float              lapStart = 0;   // time() when the current lap began
    std::vector<float> times;
    bool               wrong    = false;
    float              awayTime = 0;   // seconds driving away from the next checkpoint
    float              backTime = 0;   // seconds driving towards it again
  };

}
