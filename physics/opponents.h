#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "formats/carprofile.h"
#include "formats/settings.h"
#include "cargeometry.h"
#include "checkpoints.h"

class btBoxShape;
class btRigidBody;
struct btDefaultMotionState;

namespace rc {

class PhysicsWorld;

// How far a car has come along the route, in metres, to rank the cars of a race: the laps it has done and where
// it is on the line through the checkpoints (projected onto it). Moves on along the line, and may skip checkpoints
// ahead (some recorded laps of the opponents cut corners: surf\over1_3 leaves out the loop cp_4_1 .. cp_4_6);
// back only when the car is put back on the track.
class RouteTracker {
  public:
    explicit RouteTracker(const Checkpoints& route) : route(&route) {}

    void   reset() { seg = 0; lapsSeen = 0; value = 0; lost = false; }
    // the car was put somewhere else on the track: the next update() looks for it along the whole lap
    void   relocate() { lost = true; }
    // the car at `car` with `laps` laps done; returns progress()
    float  update(const Vec3& car, size_t laps);
    float  progress() const { return value; }

  private:
    // how far ahead along the route the car may be found at once
    static constexpr float Window = 60.f;

    const Checkpoints* route;
    size_t             seg      = 0;   // from route()[seg] to route()[seg+1]
    size_t             lapsSeen = 0;
    float              value    = 0;
    bool               lost     = false;
  };

// A computer opponent, as in the original: it replays a lap recorded on the track (CarProfile), it is not
// simulated. RCCars.exe advances the replay by `dt * rate` (0x503430), the rate being
//   CoeffCommonOpponents (Scripts/ailayouts.ini, per track: 0.85 .. 0.97)
//   x the difficulty (Scripts/aiCommonCoeffs.ini: easy 0.92, normal 1, hard 1.07)
//   x a rubber band: Splines/FuncWaitAccel<n>_A.bspl of how far (m along the route) the opponent n is ahead of
//     the player (0x4fd4b0): up to 1.1 .. 1.2 when 40 .. 50 m behind, down to 0.6 .. 0.65 when 20 .. 45 m ahead
//     (the files without _A are for another game mode, 0x4fd6e2).
// A recording is one lap from the grid whose end joins where its loop begins (CarProfile::cycleStart); the
// opponent drives the recording and its variants in turn (over1, over1_2, over1_3, over1 ..): they all start
// the same way, so they join there too. A lap counts when the replay crosses the line of cp_1 near the end of
// a recording, wherever the recorded driver went (the checkpoints only judge the player).
// Here the cars are kinematic bodies in the Bullet world: the player bumps into them and they push the props,
// but nothing pushes them off their line.
class Opponent {
  public:
    // `laps`: the recording and its variants, at least one
    Opponent(PhysicsWorld& world, const CarGeometry& geometry, std::vector<CarProfile> laps, const Checkpoints& route,
             const Spline& band);
    ~Opponent();

    // back to the first frame of the recording, on the grid
    void                 restart();
    // `dt` seconds of the race; `rate` is the replay speed without the rubber band, `ahead` metres along the route
    // this car is ahead of the player (positive) or behind (negative); `band` false: no rubber band
    void                 update(float dt, float rate, float ahead, bool band);
    // keeps the Bullet body where the car is drawn; called on every update()
    void                 place();

    Mat4                 pose() const { return current; }
    // model space pose of geometry().wheels[i]: spinning with the speed, the front ones turned into the curve
    Mat4                 wheelPose(size_t i) const;
    const CarGeometry&   geometry() const { return geom; }
    // laps done, and metres along the route since the start
    size_t               laps() const { return lapCount; }
    float                progress() const { return tracker.progress(); }
    // the replay speed the last update() used
    float                rate() const { return lastRate; }
    // m/s forward
    float                speed() const { return currentSpeed; }
    // the recordings it drives in turn, the one it is in, and the time in it
    const std::vector<CarProfile>& recordings() const { return profiles; }
    size_t               recording() const { return cur; }
    float                clock() const { return playTime; }
    // when the recording crosses cp_1's line at the end of its lap (seconds into it)
    float                lineTime(size_t i) const { return lines[i]; }

    // race bookkeeping (Opponents)
    float                finishTime = -1;   // race time of the finish, < 0 before
    std::string          name;
    int32_t              car = 0;           // 0 Overkill, 1 Buggy, 2 Hummer
    int32_t              skin = 0;          // AIPlayer<n> Skin

  private:
    PhysicsWorld&                         owner;
    CarGeometry                           geom;
    std::vector<CarProfile>               profiles;
    std::vector<float>                    lines;
    RouteTracker                          tracker;
    Spline                                bandSpline;
    size_t                                cur = 0;
    float                                 playTime = 0;
    size_t                                lapCount = 0;
    float                                 lastRate = 1;
    float                                 currentSpeed = 0;
    Mat4                                  current;
    Vec3                                  seam;          // gap left from jumping to the next recording, fades away
    float                                 wheelTurn = 0; // radians the wheels have rolled
    float                                 steerAngle = 0;
    float                                 wheelBase = 0, wheelRadius = 0.05f;
    Vec3                                  boxCenter;

    std::unique_ptr<btBoxShape>           box;
    std::unique_ptr<btDefaultMotionState> motion;
    std::unique_ptr<btRigidBody>          body;
  };

// The field of a race: the opponents of the track for a difficulty, and the standings with the player.
class Opponents {
  public:
    enum class Level { Off, Easy, Normal, Hard };
    using Reader = std::function<std::vector<uint8_t>(const std::string&)>;

    // `carScene` is RCCarsDB/Car.sb; `route` the track's checkpoints (must outlive this)
    Opponents(PhysicsWorld& world, const Reader& read, const SbScene& carScene, const std::string& track,
              const Checkpoints& route, Level level);
    ~Opponents();

    // the player's grid slot when racing against them (PlayerPath, the recording of the player's start)
    bool                 playerStart(Mat4& pose) const;
    // everyone back on the grid
    void                 restart();
    // `dt` seconds; nothing moves before `racing`. `player` is the player at `playerPos` with `playerLaps` done,
    // `playerDone` once they have finished; `raceTime` the race clock and `laps` the race's length
    void                 update(float dt, bool racing, const Vec3& playerPos, size_t playerLaps, bool playerDone,
                                float raceTime, int laps);

    size_t               size() const { return cars.size(); }
    Opponent&            operator[](size_t i) { return *cars[i]; }
    const Opponent&      operator[](size_t i) const { return *cars[i]; }
    // the player's progress along the route, as of the last update()
    float                playerProgress() const { return player.progress(); }
    // the player was put back on the track (R, drowned, the N debug jump)
    void                 playerMoved() { player.relocate(); }

    struct Standing {
      std::string name;          // empty for the player
      float       time = -1;     // finish time, < 0 still racing
      bool        player = false;
      };
    // finished cars by their time, then the others by how far they have come
    std::vector<Standing> standings(float playerFinish) const;
    // the player's place in standings(), 1 .. size()+1
    int                  place(float playerFinish) const;

    Level                level() const { return lvl; }
    static const char*   levelName(Level l);

  private:
    PhysicsWorld&                          owner;
    Level                                  lvl;
    float                                  baseRate = 1;   // CoeffCommonOpponents x difficulty
    CarProfile                             startProfile;   // PlayerPath
    RouteTracker                           player;
    std::vector<std::unique_ptr<Opponent>> cars;
  };

}
