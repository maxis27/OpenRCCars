#pragma once

#include <cstdint>
#include <vector>

#include "formats/math3d.h"

namespace rc::fx {

// A textured quad lying in the world, the way RCCars.exe hands its tyre tracks and wake to the renderer (0x45b300):
// four corners in order around it, each with a colour and two sets of texture coordinates.
struct MarkVertex {
  Vec3  pos;
  float uv[2]  = {};
  float uv2[2] = {};   // the second texture's (the wake's opacity masks)
  float rgba[4] = {1,1,1,1};
  };

struct Mark {
  enum Kind : uint8_t {
    Track,   // t_halfdry_tire2_<1..4>: multiplies what is under it by twice its colour (see game/effects.cpp)
    Wake,    // wtr_trace, its opacity times wtr_trace_alpha's
    Foam,    // whr_horse, its opacity times whr_horse_alpha's
    };
  Kind       kind    = Track;
  uint8_t    texture = 0;      // Track: the tyre (0..3)
  MarkVertex v[4];
  };

}
