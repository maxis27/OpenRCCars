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

    // what happened during an update(), for the messages and sounds: Count when "3" shows up
    enum class Event { None, Count, Go, Checkpoint, Lap, BestLap, Finish };

    // "3", "2", "1" a second each, then "Jazda!" (Language/*/msg_321_s_f.csi), as RCCars.sb scripts RaceEntry
    // ("1.0 Show3, 2.0 Show2, 3.0 Show1, 4.0 StartRace") and RaceAgain (the same from 0 to 3.0): a race entered
    // waits a second before "3", one started again does not
    static constexpr float CountTime  = 3.f;
    static constexpr float EntryDelay = 1.f;
    // the original offers 3, 5 or 7 laps (manual: "Liczba okrążeń")
    static constexpr int   DefaultLaps   = 3;

    Race(Checkpoints& route, int laps = DefaultLaps);

    // from the countdown again, the car standing at `start`; `again`: "Jeszcze raz", without the entry delay
    void   restart(const Mat4& start, bool again = false);
    // racing at once, without the countdown: the demo's DemoEntry ("0 StartRace" with no Show3 .. Show1)
    void   startNow();
    // `dt` seconds of game time with the car at `car` facing `heading` (its forward axis, world)
    Event  update(float dt, const Vec3& car, const Vec3& heading);

    Phase  phase()     const { return state; }
    // the car must stand still: before the start
    bool   holdCar()   const { return state==Phase::Countdown; }
    // seconds until the start, startTime() .. 0; the numbers show from CountTime down
    float  countdown() const { return std::max(0.f,startAt-clock); }
    // seconds from restart() to the start
    float  startTime() const { return startAt; }
    // seconds since the start (stops at the finish)
    float  time()      const { return state==Phase::Finished ? finishAt : std::max(0.f,clock-startAt); }
    // seconds since the start, going on after the finish: the clock of the opponents still racing
    float  clockTime() const { return std::max(0.f,clock-startAt); }
    // seconds since the finish, 0 before it
    float  sinceFinish() const { return state==Phase::Finished ? clockTime()-finishAt : 0.f; }
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
    // facing against the way on for a while (missed a checkpoint, or turned round), see update()
    bool   wrongWay()  const { return wrong; }
    static constexpr float WrongAfter = 6.f, WrongMax = 12.f, WrongNear = 8.f;

    const Checkpoints& checkpoints() const { return route; }

  private:
    Checkpoints&       route;
    Phase              state    = Phase::Countdown;
    int                lapCount = DefaultLaps;
    float              clock    = 0;   // since restart(), the countdown included
    float              startAt  = EntryDelay+CountTime;
    float              lapStart = 0;   // time() when the current lap began
    float              finishAt = 0;   // time() of the finish
    std::vector<float> times;
    bool               wrong    = false;
    float              wrongTime = 0;  // the wrong way timer
  };

}
