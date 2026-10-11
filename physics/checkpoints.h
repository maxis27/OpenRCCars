#pragma once

#include <string>
#include <vector>

#include "formats/collision.h"
#include "formats/scene.h"

namespace rc {

// The checkpoints of a track in driving order (markers cp_1, cp_1_1, cp_1_2 .., cp_2 .. and back to cp_1) and
// the player's progress along them. The original puts the car back at the last checkpoint it passed when the
// player presses R, drowns in the sea or falls into a hole (manual: "Wróć na tor").
class Checkpoints {
  public:
    struct Point {
      std::string name;
      Vec3        position;
      };

    // a checkpoint counts as passed within this horizontal distance (the original's radius is unknown)
    static constexpr float Radius = 2.5f;

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

    // where to put the car back: on the last checkpoint passed, facing the next one, with the model's lowest point
    // (model space Y = carBottom) on the ground; the start pose until the first checkpoint is passed
    Mat4                      respawn(float carBottom) const;

  private:
    const CollisionMesh& coll;
    std::vector<Point>   points;
    Mat4                 startPose;
    size_t               target   = 1;
    size_t               lapCount = 0;
    bool                 passed   = false; // any checkpoint since reset()
  };

}
