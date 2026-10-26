#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "formats/math3d.h"

namespace rc {
struct SbScene;
}

namespace rc::fx {

// The track's moving pieces of scenery, RCCars.exe's "AnimObj" (VIDEO_AnimObj, set up at 0x52b7e0 and 0x52c110,
// moved every frame the game is not paused by 0x52b860 and 0x52c180, both from the game's time 0x4a0da0):
// - submarines: every node with "SUBMARINE" in its name (beach_4's SUBMARINE_NC, at most 10) gets the local
//   transform translate(0, Y0 + DY cos(T t), 0) (Settings\submarine.ini, 0x52c2f0: Y0 -1/100 of its slider, DY
//   and T 1/100, so -0.96 m, 1.44 m and 0.17 rad/s): it comes up and goes down again every 37 s;
// - radars: under a node with "radar_ZRK" in its name the first child (the last in the file) must have "center"
//   in its name; the child before it (with its first child and that child's neighbour, if it has children) turns
//   round the vertical through the middle of the centre's box at 60 degrees a second (0x52b930, 0x52b860).
// The meshes stay where they are in the scene's own data; pose() is what moves them, in world space.
class AnimatedObjects {
  public:
    struct Settings {
      float y0 = -0.96f, dy = 1.44f, t = 0.17f;   // submarine.ini
      using Reader = std::function<std::vector<uint8_t>(const std::string&)>;
      static Settings load(const Reader& read);
      };

    AnimatedObjects() = default;
    AnimatedObjects(const SbScene& scene, const Settings& settings);

    // which group each of the scene's meshes is moved with (1 ..), 0 for those that stay; children go with
    // their parents
    const std::vector<int32_t>& groups() const { return groupOf; }
    size_t size() const { return items.size(); }
    // the move of group `g` (1 ..) at `time` s: world space, from the scene as modelled
    Mat4   pose(int32_t g, float time) const;
    // the poses of all groups, index 0 the identity (for LevelMesh::draw)
    std::vector<Mat4> poses(float time) const;

  private:
    struct Item {
      enum Kind { Submarine, Radar } kind = Submarine;
      Mat4 parent;        // world transform of the node's parent
      Mat4 toLocal;       // inverse of the node's world transform as modelled
      Vec3 centre;        // radar: the pivot, in the parent's space
      };
    Settings             set;
    std::vector<Item>    items;
    std::vector<int32_t> groupOf;
  };

}
