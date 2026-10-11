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
  };

CarGeometry carGeometry(const SbScene& sb, std::string_view model);

}
