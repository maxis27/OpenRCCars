#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/scene.h"

namespace rc {

// Which part of a car from RCCarsDB/Car.sb a mesh belongs to: "" for the body, "WHEEL_..." for the wheel node
// above it (or itself), nullopt for meshes that are not shown (UPGRADES* add-ons, _LOD copies).
std::optional<std::string> carPart(const SbScene& sb, size_t mesh);
// The same for drawing: the nearest node above the mesh (or itself) that moves on its own (CarRig): a WHEEL_*
// node or one of the suspension's axles, arms, steering supports and springs; "" for the body.
std::optional<std::string> carNode(const SbScene& sb, size_t mesh);

// Physical layout of one car model, in model space (+Z forward, +Y up, +X left), from its render geometry.
struct CarGeometry {
  struct Wheel {
    std::string name;      // WHEEL_FRONT_LEFT ...
    Vec3        center;    // hub, centre of the wheel's bounding box
    float       radius = 0;
    float       width  = 0;
    bool        front  = false;
    bool        left   = false;
    // the suspension nodes it hangs from (empty names if the model has none): the rigid axle (index in axles),
    // the steering support (Overkill, Hummer) or the Buggy's upper and lower arm and the wheel carrier between them
    int         axle   = -1;
    std::string support, armUp, armDown, carrier;
    Vec3        supportPivot, upPivot, downPivot, carrierPivot;
    };
  std::vector<Wheel> wheels;
  // rigid axles: FRONT_AXLE, REAR_AXLE and the Hummer's MIDDLE_AXLE; pivot = the node's origin
  struct Axle {
    std::string name;
    Vec3        pivot;
    int         left = -1, right = -1; // wheels
    bool        middle = false;
    };
  std::vector<Axle>  axles;
  // springs fixed to the body (SPRING_FRONT_1.., Spring1_2..) that reach from `pivot` to `end`, a point that moves
  // with the node `anchor` (an axle or the Buggy's lower arm). The Overkill's have no end in the model: they reach
  // CarParams::strutLength along `axis`, the node's Z axis (zero for the others)
  struct Strut {
    std::string name;
    Vec3        pivot, end, axis;
    std::string anchor;
    };
  std::vector<Strut> struts;
  Vec3               bodyMin, bodyMax; // bounding box of everything but the wheels
  Vec3               min, max;         // whole car
  float              roof = 0;         // top of the car without the thin antenna (how deep it sits in water)
  // UPGRADES/booster_<n>_end, n = 1..4: where the exhaust leaves each of the four boosters (RCCars.exe 0x50c0e0
  // keeps the node's position and its Z axis in car space; 0x50bcc0 hands them to the exhaust smoke)
  struct Exhaust {
    Vec3 pos;
    Vec3 dir;              // unit
    };
  std::vector<Exhaust> exhausts;       // empty if the model has no such nodes
  };

CarGeometry carGeometry(const SbScene& sb, std::string_view model);

}
