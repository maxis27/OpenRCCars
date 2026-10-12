#pragma once

#include <string>
#include <vector>

#include "formats/collision.h"
#include "formats/scene.h"

namespace rc {

// The checkpoints of a track in driving order (markers cp_1, cp_1_1, cp_1_2 .., cp_2 .. and back to cp_1) and
// the player's progress along them. Only the main checkpoints cp_<n> count: a lap is passing all of them in order,
// and R, drowning or falling into a hole puts the car back on the last one passed (manual: "Wróć na tor"). The
// sub-checkpoints cp_<n>_<m> between them show the way: the arrow points at the next one.
//
// As in RCCars.exe, the next main checkpoint counts once the car (its origin) comes within PassRadius of the
// marker, measured in 3D; the computer's cars get AiPassRadius. Only the next one is tested, so the order holds by
// itself. The markers' `width`, `height` and `expand` (MARK properties) describe in the original the route
// corridor from one marker to the next, from which race progress and places are worked out; here only `width`
// helps the arrow move on.
class Checkpoints {
  public:
    struct Point {
      std::string name;
      Vec3        position;
      Vec3        dir;              // horizontal unit vector along the route through this checkpoint
      float       width  = DefaultWidth;
      float       height = DefaultHeight;
      float       expand = DefaultExpand;
      float       ground = 0;       // world height of the ground right under the marker
      bool        main   = false;   // cp_<n>, not a sub-checkpoint cp_<n>_<m>
      };

    // used where a marker has no property of its own (all of country_1 and most of country_2, a guess)
    static constexpr float DefaultWidth  = 6.f;
    static constexpr float DefaultHeight = 2.f;
    static constexpr float DefaultExpand = 1.f;
    // RCCars.exe: a main checkpoint counts within this distance of its marker, for the player and for the computer
    static constexpr float PassRadius    = 1.6f;
    static constexpr float AiPassRadius  = 2.88f;
    // how far below the marker to look for the ground under it
    static constexpr float MaxDepth      = 4.f;
    // a sub-checkpoint is left behind once the car crosses its line (across the route) this close to it
    static constexpr float GuideRadius   = 15.f;
    // .. or comes this close to the side of its corridor (`width`)
    static constexpr float NearMargin    = 4.f;

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

    // whether moving from `from` to `to` comes within `radius` of the marker of route()[i]
    bool                      passes(size_t i, const Vec3& from, const Vec3& to, float radius = PassRadius) const;
    // the main checkpoint after route()[i]
    size_t                    nextMain(size_t i) const;

    // where to put the car back: on the last main checkpoint passed, facing the way on, with the model's lowest
    // point (model space Y = carBottom) on the ground; the start pose until the first one is passed. The arrow
    // starts again from there.
    Mat4                      respawn(float carBottom);

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
  };

}
