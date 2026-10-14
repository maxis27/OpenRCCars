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

// Physical layout of one car model, in model space (+Z forward, +Y up, +X left), from its render geometry.
struct CarGeometry {
  struct Wheel {
    std::string name;      // WHEEL_FRONT_LEFT ...
    Vec3        center;    // hub, centre of the wheel's bounding box
    float       radius = 0;
    float       width  = 0;
    bool        front  = false;
    bool        left   = false;
    };
  std::vector<Wheel> wheels;
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
