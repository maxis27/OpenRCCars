#pragma once

#include <optional>
#include <random>
#include <vector>

#include "formats/math3d.h"
#include "soundlib.h"

namespace rc {

struct SbScene;
class  SliderFile;

// Sounds of a track's water as RCCars.exe plays them, heard from the camera:
//  - water_splash_N markers (0x5277a0 / 0x527a40): waves splashing on the shore. Each one within ActiveRad m of the
//    camera loops water_splash, moved every TimeDelay +- TimeDelayDisp s to a new spot within Radius m of the marker
//    (Settings\water_splash_mng.ini, in 1/100). The original also wants the marker on screen, and shows a splash.
//  - WATER_FALL_N markers (0x51d440): water_fall loops at each one.
//  - the sea (0x522ff0 / 0x522f60 / 0x523140): channel "water" loops at the corner of the sea's outline nearest to
//    the camera in the xz plane. When there is water (any, streams too) within 10 m above or below the camera, it
//    moves right under the camera instead, onto that water or onto the camera if it is under water. A track without
//    a sea has no sea sound, not even over its streams.
// All channels are positioned (RCCars.sb: splash gone 3 m away, waterfall and sea 10 m, the sea full up to 3 m).
class AmbientSounds {
  public:
    AmbientSounds() = default;
    // `coast`: the corners of the sea's outline (LevelMesh::seaCoast), empty if the track has no sea
    AmbientSounds(Mixer& mixer, SoundLibrary& lib, const SbScene& scene, const SliderFile& splashSettings,
                  std::vector<Vec3> coast);

    // `ear` and `right`: the listener (camera) position and its right-hand direction; `waterAtEar`: the height of
    // the highest water surface between 10 m above and 10 m below the camera, if any
    void update(const Vec3& ear, const Vec3& right, float dt, std::optional<float> waterAtEar = std::nullopt);
    void stop();

  private:
    struct Splash {
      Vec3        marker;
      Vec3        spot;
      BankChannel channel;
      bool        active = false;
      float       timer  = 0;
      };
    struct Fall {
      Vec3        at;
      BankChannel channel;
      };

    std::vector<Splash> splashes;
    std::vector<Fall>   falls;
    std::vector<Vec3>   coast;
    BankChannel         sea;
    float               delay = 1.f, delayDisp = 0.25f, radius = 0.05f, activeRad = 20.f;
    std::mt19937        rng{12345};
  };

}
