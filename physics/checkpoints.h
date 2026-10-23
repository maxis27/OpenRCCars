#pragma once

#include <string>
#include <vector>

#include "formats/collision.h"
#include "formats/scene.h"

namespace rc {

// The checkpoints of a track in driving order (markers cp_1, cp_1_1, cp_1_2 .., cp_2 .. and back to cp_1) and
// the player's progress along them. Only the main checkpoints cp_<n> count: a lap is passing all of them in order,
// and R, drowning or falling into a hole puts the car back on the last one passed (manual: "Wróć na tor"). The
// sub-checkpoints cp_<n>_<m> between them show the way.
//
// As in RCCars.exe, the next main checkpoint counts once the car (its origin) comes within PassRadius of the
// marker, measured in 3D; the computer's cars get AiPassRadius. Only the next one is tested, so the order holds by
// itself. RCCars.exe (0x4e9560) takes the sub-checkpoints cp_<n>_1, cp_<n>_2 .. up to the first number missing, and
// at most 8 main ones. From each point to the next it puts a box, the route corridor (0x4e9980, Piece), sized by
// the `width`, `height` and `expand` of the point it starts from (or of the folder "Checkpoints", or 2, 1 and 0.1
// m). Where the car is in the corridor gives the way on (the arrow, the wrong way warning) and how much of the way
// to the next main checkpoint is left (the arrow's colour).
class Checkpoints {
  public:
    struct Point {
      std::string name;
      Vec3        position;
      Vec3        dir;              // horizontal unit vector along the route through this checkpoint
      float       width  = DefaultWidth;   // m from the middle of the corridor to its side
      float       height = DefaultHeight;  // m from its middle up (and down)
      float       expand = DefaultExpand;  // m it goes on past both ends
      float       ground = 0;       // world height of the ground right under the marker
      bool        main   = false;   // cp_<n>, not a sub-checkpoint cp_<n>_<m>
      };

    // used where neither a marker nor the folder "Checkpoints" has the property (RCCars.exe 0x4e9560)
    static constexpr float DefaultWidth  = 2.f;
    static constexpr float DefaultHeight = 1.f;
    static constexpr float DefaultExpand = 0.1f;
    // RCCars.exe: a main checkpoint counts within this distance of its marker, for the player and for the computer
    static constexpr float PassRadius    = 1.6f;
    static constexpr float AiPassRadius  = 2.88f;
    // how far below the marker to look for the ground under it
    static constexpr float MaxDepth      = 4.f;
    // a sub-checkpoint is left behind once the car crosses its line (across the route) this close to it
    static constexpr float GuideRadius   = 15.f;
    // .. or comes this close to the side of its corridor (`width`)
    static constexpr float NearMargin    = 4.f;

    // the route corridor from route()[i] to route()[i+1] (RCCars.exe 0x4e9980): a box along `dir` from `origin`,
    // `expand` longer at both ends, `width` to either side (along `side`) and `height` up and down (along `up`, the
    // world's up square to `dir`)
    struct Piece {
      Vec3  origin, dir, up, side;
      float length = 0, expand = 0, width = 0, height = 0;
      float along  = 0;               // m of the corridor before it, in 3D
      };

    Checkpoints(const SbScene& scene, const CollisionMesh& track);

    // route()[0] is cp_1 and the last point is cp_1 again; empty if the track has no checkpoints
    const std::vector<Point>& route() const { return points; }
    bool                      empty() const { return points.empty(); }

    // back to the start: the car stands at `start` and heads for route()[1]
    void                      reset(const Mat4& start);
    // call with the car's position every frame; true if it has just passed the next main checkpoint
    bool                      update(const Vec3& car);
    // index of the main checkpoint the car has to pass next, 1 .. route().size()-1
    size_t                    next() const { return target; }
    // index of the point the arrow shows: the next sub-checkpoint on the way to next(), or next() itself
    size_t                    guide() const { return way; }
    // debug (N): the arrow straight to next()
    void                      skipWay() { way = target; }
    // index of the main checkpoint passed last (0 = cp_1, the start)
    size_t                    lastPassed() const { return lastMain; }
    // main checkpoints in a lap (cp_2 .. cp_<n> and cp_1 again), and how many of them are passed in this lap
    size_t                    mainCount() const { return mains.size()>1 ? mains.size()-1 : 0; }
    size_t                    mainPassed() const;
    // laps finished since reset()
    size_t                    laps() const { return lapCount; }
    // metres along the route from cp_1 to route()[i] (horizontally, straight from point to point), and the whole lap
    float                     along(size_t i) const { return distances[i]; }
    float                     lapLength() const { return distances.empty() ? 0.f : distances.back(); }

    // the corridor, piece i from route()[i] to route()[i+1]
    const std::vector<Piece>& corridor() const { return pieces; }
    // the way on from `car` (RCCars.exe 0x4e8f00), not of unit length: inside the part of the corridor up to the
    // next main checkpoint along it, in another part along it (or back along it, past the next main checkpoint),
    // and outside it straight to the next main checkpoint
    Vec3                      wayOn(const Vec3& car) const;
    // the part of the way from the last main checkpoint to the next one still to go, along the corridor, 0..1
    // (RCCars.exe 0x4eb670)
    float                     wayLeft(const Vec3& car) const;

    // whether moving from `from` to `to` comes within `radius` of the marker of route()[i]
    bool                      passes(size_t i, const Vec3& from, const Vec3& to, float radius = PassRadius) const;
    // the main checkpoint after route()[i]
    size_t                    nextMain(size_t i) const;

    // where to put the car back: on the last main checkpoint passed, facing the way on, with the model's lowest
    // point (model space Y = carBottom) on the ground; the start pose until the first one is passed. The arrow
    // starts again from there.
    Mat4                      respawn(float carBottom);
    // on route()[i], facing route()[i+1], with the model's lowest point (Y = carBottom) on the ground
    Mat4                      poseAt(size_t i, float carBottom) const;

  private:
    const CollisionMesh& coll;
    std::vector<Point>   points;
    std::vector<size_t>  mains;             // indices of the main checkpoints, 0 .. points.size()-1
    std::vector<float>   distances;         // metres along the route from cp_1 to each point
    Mat4                 startPose;
    Vec3                 last;              // car position at the previous update()
    bool                 hasLast  = false;
    size_t               target   = 1;
    size_t               way      = 1;
    size_t               lastMain = 0;
    size_t               lapCount = 0;
    bool                 passed   = false; // any checkpoint since reset()
    // the corridor, and where the car was found in it last (RCCars.exe tracker 0x38 .. 0x48)
    std::vector<Piece>   pieces;
    float                corridorLength = 0;
    enum class In : uint8_t { Nowhere, Ahead, Elsewhere } in = In::Nowhere; // 0, 1: the part up to the next main
                                                                          // checkpoint, 2: another one
    size_t               piece    = 0;
    float                frac     = 0;     // how far along it, 0..1
    bool                 before   = true;  // Elsewhere: still short of the next main checkpoint (0x48)
    void                 locate(const Vec3& car);   // 0x4eba30 and 0x4eb670
    bool                 inside(const Piece& p, const Vec3& car, float& f) const; // 0x4ebb40
    float                alongTo(size_t i) const;   // corridor m from cp_1 to route()[i]
    size_t               mainBefore(size_t i) const;
  };

}
