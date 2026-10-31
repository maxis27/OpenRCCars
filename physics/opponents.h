#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "formats/carprofile.h"
#include "formats/settings.h"
#include "cargeometry.h"
#include "carparams.h"
#include "carrig.h"
#include "checkpoints.h"
#include "resetvolumes.h"
#include "vehicle.h"


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
    // the car is between route()[segment()] and the point after it
    size_t segment() const { return seg; }

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
// While it replays, nothing pushes the car. When another car or a prop comes at it, it is knocked off the
// recording (knock(), RCCars.exe 0x533990, 0x533ee0 -> 0x4fd9d0): from then on it is a car like the player's (Vehicle, with its own CarParams), driven by the game's autopilot back
// onto the recording (0x4fdb50 .. 0x4fe490, see drive()), and replays again once it is there.
// The game puts its cars back on the track as it does the player's (0x508160, see trackRules()): out of the
// collision box, drowned or in a reset volume. The car goes to the last main checkpoint it has passed (its grid
// slot before the first one), knocked off and driving back onto its recording from there (0x508b50).
class Opponent {
  public:
    // `laps`: the recording and its variants, at least one; `params` drive the car once it is knocked off
    Opponent(PhysicsWorld& world, const CarGeometry& geometry, std::vector<CarProfile> laps, const Checkpoints& route,
             const Spline& band, const CarParams& params);
    ~Opponent();

    // back to the first frame of the recording, on the grid
    void                 restart();
    // `dt` seconds of the race; `rate` is the replay speed without the rubber band, `ahead` metres along the route
    // this car is ahead of the player (positive) or behind (negative); `band` false: no rubber band. `seen`: a
    // point is in sight (a car knocked off long ago is put back on its recording only where nobody sees either it
    // or the frame it goes to); none: everything is
    void                 update(float dt, float rate, float ahead, bool band,
                                const std::function<bool(const Vec3&)>& seen = {});

    // off the recording, driving back to it
    bool                 knocked() const { return driven!=nullptr; }
    // something came at the car: off the recording, if it was on it
    void                 knock();
    // the spheres the other objects meet (0x4ef9e0): the recording's car where it is drawn, or the knocked car's
    void                 balls(std::vector<Ball>& out) const;
    // the knocked car, nullptr while it replays
    Vehicle*             vehicle() { return driven.get(); }
    // world, m/s and rad/s in the car's axes
    Vec3                 velocity() const;
    Vec3                 localSpin() const;
    // a car came at it (any contact with a car resets this)
    void                 touched() { sinceTouch = 0; }
    // the track's collision box and reset volumes (Opponents::setTrack); none: the car is never put back
    void                 setTrack(const Vec3& boxMin, const Vec3& boxMax, const ResetVolumes& volumes);
    // why trackRules() put the car back
    enum class Back { None, OutOfBox, Drowned, Volume, Shot, Stuck };
    // RCCars.exe 0x508160 after every update() of the race: out of the collision box (sideways, or 0.1 m under
    // it), drowned (Vehicle::drownRate), longer than FilterTime in a reset volume, or knocked off and in any reset
    // volume (0x4fe3f0): put back on the track. Says why, Back::None if it was not
    Back                 trackRules(float dt);
    // back on the track at the last main checkpoint passed
    void                 putBack();
    // over the finish line (RCCars.exe 0x4eaad0): off the recording, no keys and Brake held, the wheels turned
    // straight; FinishStop s later it stands where it is (0x4f6d0e, 0x4f7341)
    void                 finish();
    bool                 finished() const { return stopTime>=0; }
    static constexpr float FinishStop  = 2.f;   // s
    static constexpr float FinishSteer = 120.f; // the wheels' degrees a second back to straight
    // a bullet hit it (RCCars.exe 0x508560): knocked off its recording and pushed away from `gun`
    // (Vehicle::shotFrom); put back on the track Vehicle::ShotHold s after the first hit (0x508ae0, in trackRules)
    void                 shotFrom(const Vec3& gun);

    Mat4                 pose() const;
    // how the wheels and the suspension are drawn now (CarRig): driven, as the Vehicle has them; following the
    // recording, at their modelled heights, spinning with the speed, the front ones turned into the curve
    CarRig::Pose         rigPose() const;
    // model space pose of geometry().wheels[i] as drawn
    Mat4                 wheelPose(size_t i) const;
    const CarGeometry&   geometry() const { return geom; }
    // laps done, and metres along the route since the start
    size_t               laps() const { return lapCount; }
    float                progress() const { return tracker.progress(); }
    // the replay speed the last update() used
    float                rate() const { return lastRate; }
    // m/s forward
    float                speed() const;
    // the recordings it drives in turn, the one it is in, and the time in it
    const std::vector<CarProfile>& recordings() const { return profiles; }
    size_t               recording() const { return cur; }
    float                clock() const { return playTime; }
    // when the recording crosses cp_1's line at the end of its lap (seconds into it)
    float                lineTime(size_t i) const { return lines[i]; }

    // race bookkeeping (Opponents)
    float                finishTime = -1;   // race time of the finish, < 0 before
    float                lapAt      = 0;    // race time the lap being driven began
    float                bestLap    = -1;   // its fastest lap, < 0 none yet
    float                sinceCar   = 1e3f; // s since a hit with another car began (Opponents::collide)
    float                shakeLeft  = 0;    // s left of the window judging how hard the player hit it ...
    Vec3                 shake;             // ... and its rotation in that window so far, own axes, rad
    std::string          name;              // the face picture's name ("MCJocker"), "AI<n>" without a driver
    int32_t              driver = 0;        // AIPlayer<n>, 0 none: the name is Language/Game 40499+n (0x4a9ce0)
    std::string          face;              // AIPlayer<n> Face: a picture of FacesSys ("MCJocker.tga")
    int32_t              car = 0;           // 0 Overkill, 1 Buggy, 2 Hummer
    int32_t              skin = 0;          // AIPlayer<n> Skin
    int32_t              boost = 0, reson = 0, tires = 0; // AIRace<n> AI<n>Boost, Reson, Tires: how it looks
    size_t               putBacks = 0;      // times put back on the track
    float                shotTime = -1;     // s since a bullet hit it, < 0 none

  private:
    // the autopilot of a knocked car
    void                                  drive(float dt, const std::function<bool(const Vec3&)>& seen);
    // back on the recording at frame `frame` of the current one
    void                                  resume(size_t frame);
    // the point of the current recording at its frame `i`
    Vec3                                  framePoint(size_t i) const;

    PhysicsWorld&                         owner;
    const Checkpoints&                    checkpoints;
    CarGeometry                           geom;
    CarParams                             prm;
    CarRig                                rig;
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

    // knocked off the recording (RCCars.exe keeps these at car+0x4390 ..)
    std::unique_ptr<Vehicle>              driven;
    float                                 driveTime  = 0;    // s since knocked (0x439c)
    float                                 sinceTouch = 1e3f; // s since the last contact (0x4430)
    float                                 stuckTime  = 0;    // s barely moving (0x442c) ...
    float                                 backTime   = 0;    // ... backing up for this long (0x4428)
    bool                                  backing    = false;
    float                                 steerDeg   = 0;    // the autopilot's wheel angle, + = left (0x5c6c)
    float                                 stopTime   = -1;   // s since the finish (car+0x128), < 0 before
    Mat4                                  stopPose;          // where it stands FinishStop s after it
    size_t                                nearFrame  = 0;    // nearest frame of the recording so far (0x43a0)
    // back on the recording: the knocked car's pose relative to the recording's fades away (0x503190)
    Mat4                                  resumeOffset;
    float                                 resumeFade = 0;

    // putting it back on the track
    bool                                  hasTrack   = false;
    Vec3                                  boxMin, boxMax;
    ResetVolumes                          resets;
    float                                 drownTimer = 0;
  };

// The field of a race: the opponents of the track for a difficulty, and the standings with the player.
class Opponents {
  public:
    enum class Level { Off, Easy, Normal, Hard };
    using Reader = std::function<std::vector<uint8_t>(const std::string&)>;

    using Params = std::function<CarParams(int32_t car)>;   // car: 1 Overkill, 2 Buggy, 3 Hummer

    // `carScene` is RCCarsDB/Car.sb; `route` the track's checkpoints (must outlive this); `params` the driving
    // parameters of a knocked car (CarParams::load), defaults without it. `demo`: the game's demo (mode 6,
    // RCCars.exe 0x4a5320): the track's Demo1 .. Demo5 instead of the opponents, every one of them racing, the
    // car of its driver (AIPlayer<n> Car, 0x4a9ce0) without upgrades; see follow()
    Opponents(PhysicsWorld& world, const Reader& read, const SbScene& carScene, const std::string& track,
              const Checkpoints& route, Level level, const Params& params = {}, bool demo = false);
    ~Opponents();

    // the track's collision box and reset volumes, for putting the cars back on the track (Opponent::trackRules)
    void                 setTrack(const SbScene& track);
    // the player's grid slot when racing against them (PlayerPath, the recording of the player's start)
    bool                 playerStart(Mat4& pose) const;
    // everyone back on the grid
    void                 restart();
    // what happened to an opponent in an update(), for the race's chat console (RCCars.exe 0x4eaa50, 0x4eaad0,
    // 0x508a10, 0x509690, 0x4fe3f0)
    struct News {
      enum What { Lap, Finished, Drowned, Volume } what = Lap;
      size_t car  = 0;
      size_t laps = 0;   // Lap: laps done
      };
    // `dt` seconds; nothing moves before `racing`. `player` is the player at `playerPos` with `playerLaps` done,
    // `playerDone` once they have finished; `raceTime` the race clock and `laps` the race's length
    void                 update(float dt, bool racing, const Vec3& playerPos, size_t playerLaps, bool playerDone,
                                float raceTime, int laps);
    // what update() has seen
    const std::vector<News>& news() const { return told; }
    // the cameras of the views, which tell where a knocked car may be put back unseen (RCCars.exe asks every view,
    // 0x487300); none: the player's car looks forward from where it is
    struct Viewer { Vec3 eye, forward; };
    void                 setViewers(std::vector<Viewer> v) { viewers = std::move(v); }
    // the demo: the car `i` is the active one (the script's SetActive '$CAR' <i>) instead of the player: the
    // rubber band measures the others against it (RCCars.exe 0x4fd5e0 takes the active car, 0x4870c0) and it is
    // the one that sees whether a car stuck behind it may be put back; -1 the player again
    void                 follow(int i) { focus = i; }
    int                  followed() const { return focus; }

    size_t               size() const { return cars.size(); }
    Opponent&            operator[](size_t i) { return *cars[i]; }
    const Opponent&      operator[](size_t i) const { return *cars[i]; }
    // the player's progress along the route, as of the last update()
    float                playerProgress() const { return player.progress(); }
    // the player was put back on the track (R, drowned, the N debug jump)
    void                 playerMoved() { player.relocate(); }

    // RCCars.exe's car against car (0x533990, 0x5334b0, 0x4f6460), once per frame before update(): every car that
    // touches another one is knocked off its recording, and the two get the original's push. A hit of the player
    // on an opponent, both 2 s after their last one, opens a half-second window on the opponent: knocked about
    // enough in it (by its rotation), it is a hit or a great hit for the player, worth Vehicle::HitBoost turbo
    // units (already added). Returns what the player got this frame.
    Vehicle::Hit         collide(Vehicle& player, float dt);

    struct Standing {
      std::string name;          // empty for the player
      float       time = -1;     // finish time, < 0 still racing
      bool        player = false;
      int         car    = -1;   // the opponent's index, -1 for the player
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
    Mat4                                   playerPose;     // as of the last collide()
    std::vector<Viewer>                    viewers;
    float                                  playerSinceCar = 1e3f;
    float                                  baseRate = 1;   // CoeffCommonOpponents x difficulty
    CarProfile                             startProfile;   // PlayerPath
    RouteTracker                           player;
    std::vector<std::unique_ptr<Opponent>> cars;
    std::vector<News>                      told;
    int                                    focus = -1;     // follow()
    std::vector<std::vector<Ball>>         ballBuf;        // collide(): the player's, then each opponent's
  };

}
