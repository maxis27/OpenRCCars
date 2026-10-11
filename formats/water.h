#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "math3d.h"
#include "settings.h"

namespace rc {

class  PackFile;
struct SbScene;

// The sea of a track: Settings\WLOD_<track>_<object>.ini, e.g. WLOD_beach_1_more1.ini for the faces under the
// node "more1". Read from the original's loader (RCCars.exe 0x521540): each slider is clamped to its range and
// scaled as noted. The sea moves every vertex by
//   y = y0 + offset + (1-s)*magnetOffset + s*(wave1 + wave2),       s = clamp(edge/magnetRadius, 0, 1)
//   wave1 = amp  * (0.75*sin(period*(x*cos(angle) + z*sin(angle) + speed*t)) + 0.25)
//   wave2 = amp2 * sin(|xz - center|/length2 + period2*t)
// where `edge` is the vertex's distance to the outline of the sea (only inside the scene's "water_box" volume,
// elsewhere s = 1). Wave 1 comes from `angle`, wave 2 runs in to `center`. The vertex alpha grows from 35/255
// at the outline to 1 at alphaRadius, near the cars only (distAlphaClamp*), and its grey from colorMin to colorMax
// with the height of the waves.
struct WaterWaves {
  float amp = 0, angle = 0, speed = 0, period = 0;   // amp/100 m, angle*10 deg, speed/100, period/10
  float amp2 = 0, period2 = 0, length2 = 1;          // amp2/100 m, period2/10, length2/100
  Vec3  center;                                      // posX-500, 0, posZ-500
  float offset = 0, magnetOffset = 0;                // value/100 - 0.5 m
  float magnetRadius = 0, alphaRadius = 0;           // value/20 m, value/10 m
  float distAlphaClampMin = 0, distAlphaClampMax = 0;
  float colorMin = 255, colorMax = 255;              // 0..255

  // nullopt for files without waves (e.g. WLOD_beach_1_wtr_fall.ini, a waterfall)
  static std::optional<WaterWaves> parse(const SliderFile& f);
  };

// Settings\waterLOD_coast.ini, the foam on the beaches (RCCars.exe 0x51cef0, used at 0x51c690). The foam follows
// wave 2 of the sea: with h = sin(phase of wave 2) at the foam,
//   falling water:         alpha from alphaMax (h = -1) down to alphaMin (h = 1)
//   rising water:          alpha from alphaMax (h = -1) down to alphaMin (h = heightOn), alphaMin above
// so the foam shows as the water draws back and fades as the next wave comes in. Its texture scrolls by scrollVel
// per second. "HeightOff" is read but never used.
struct CoastFoam {
  float heightOn = 0;                  // (value-100)/100
  float scrollVel = 0;                 // value/100
  float alphaMin = 0, alphaMax = 1;    // value/255

  static CoastFoam parse(const SliderFile& f);
  };

// The seas of a track: each mesh belongs to the nearest node above it (or itself) that has a
// WLOD_<track>_<node>.ini with waves, e.g. "more1" on beach_1.
struct Seas {
  std::vector<std::string> names;
  std::vector<WaterWaves>  waves;
  std::vector<int32_t>     ofMesh; // per SbScene::meshes: index into names/waves, -1 if none

  static Seas load(const SbScene& scene, PackFile& pack, std::string_view track);
  };

}
