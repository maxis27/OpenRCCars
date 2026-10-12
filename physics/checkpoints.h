#pragma once

#include <string>
#include <vector>

#include "formats/collision.h"
#include "formats/scene.h"

namespace rc {

// The checkpoints of a track in driving order (markers cp_1, cp_1_1, cp_1_2 .., cp_2 .. and back to cp_1) and
// the player's progress along them. The original puts the car back at the last checkpoint it passed when the
// player presses R, drowns in the sea or falls into a hole (manual: "Wróć na tor").
//
// Every checkpoint is a gate: a vertical rectangle across the route, `width` wide, from the ground up to `height`
// above the marker (markers float up to 2.7 m above the ground, beach_4), facing along the route (the markers
// carry no rotation). The level designers set the sizes per marker (MARK properties `width`, `height`, `expand`);
// `expand` is read as how far in front of and behind the gate the car still counts as having passed it. A lap
// means passing all gates in order, as in the manual ("zaliczenie wszystkich punktów kontrolnych we właściwej
// kolejności").
class Checkpoints {
  public:
    struct Point {
      std::string name;
      Vec3        position;
      Vec3        dir;              // horizontal unit vector along the route through this checkpoint
      float       width  = DefaultWidth;
      float       height = DefaultHeight;
      float       expand = DefaultExpand;
      float       bottom = 0;       // world height of the gate's lower edge: the ground under the marker
      bool        main   = false;   // cp_<n>, not a sub-checkpoint cp_<n>_<m>
      };

    // used where a marker has no property of its own (all of country_1 and most of country_2, a guess)
    static constexpr float DefaultWidth  = 6.f;
    static constexpr float DefaultHeight = 2.f;
    static constexpr float DefaultExpand = 1.f;

    Checkpoints(const SbScene& scene, const CollisionMesh& track);

    // route()[0] is cp_1 and the last point is cp_1 again; empty if the track has no checkpoints
    const std::vector<Point>& route() const { return points; }
    bool                      empty() const { return points.empty(); }

    // back to the start: the car stands at `start` and heads for route()[1]
    void                      reset(const Mat4& start);
    // call with the car's position every frame; true if it has just passed the next checkpoint
    bool                      update(const Vec3& car);
    // index of the checkpoint the car heads for, 1 .. route().size()-1
    size_t                    next() const { return target; }
    // laps finished since reset()
    size_t                    laps() const { return lapCount; }

    // whether moving from `from` to `to` passes the gate of route()[i]
    bool                      passes(size_t i, const Vec3& from, const Vec3& to) const;

    // where to put the car back: on the last checkpoint passed, facing the next one, with the model's lowest point
    // (model space Y = carBottom) on the ground; the start pose until the first checkpoint is passed
    Mat4                      respawn(float carBottom) const;

  private:
    const CollisionMesh& coll;
    std::vector<Point>   points;
    Mat4                 startPose;
    Vec3                 last;              // car position at the previous update()
    bool                 hasLast  = false;
    size_t               target   = 1;
    size_t               lapCount = 0;
    bool                 passed   = false; // any checkpoint since reset()
  };

}
