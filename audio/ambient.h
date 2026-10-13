#pragma once

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
// Both channels are positioned (RCCars.sb: splash gone 3 m away, waterfall 10 m).
class AmbientSounds {
  public:
    AmbientSounds() = default;
    AmbientSounds(Mixer& mixer, SoundLibrary& lib, const SbScene& scene, const SliderFile& splashSettings);

    // `ear` and `right`: the listener (camera) position and its right-hand direction
    void update(const Vec3& ear, const Vec3& right, float dt);
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
    float               delay = 1.f, delayDisp = 0.25f, radius = 0.05f, activeRad = 20.f;
    std::mt19937        rng{12345};
  };

}
