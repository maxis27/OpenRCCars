#pragma once

#include <string>
#include <vector>

#include "formats/scene.h"

namespace rc {

// The track's "car_reset*" volumes (EVOL car_reset_volume_1, _bereg, _fort ...), spots picked by the level
// designers on beach_2, beach_3, country_2 and country_4. As in RCCars.exe (0x509570, 0x509690), a car that stays
// in the same one for longer than its FilterTime is put back on the track at the last checkpoint passed, like R.
// FilterTime comes from the volume's script in ms (Params, default 10 ms) and ranges from 10 ms to 3 s.
class ResetVolumes {
  public:
    struct Volume {
      const SbVolume* volume = nullptr;
      float           filter = DefaultFilter; // s
      };

    static constexpr size_t MaxVolumes    = 16;    // the game takes the first 16
    static constexpr float  DefaultFilter = 0.01f; // s, when the script has no FilterTime

    ResetVolumes() = default;
    explicit ResetVolumes(const SbScene& track);

    const std::vector<Volume>& volumes() const { return list; }

    // the car's origin this frame: true once it is time to put the car back on the track
    bool tick(const Vec3& at, float dt);
    // the car was put back somewhere else
    void reset() { inside = -1; timer = 0; }

  private:
    std::vector<Volume> list;
    int                 inside = -1; // the volume the car is in, -1 for none
    float               timer  = 0;  // s in it
  };

}
