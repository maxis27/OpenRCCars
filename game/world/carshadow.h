#pragma once

#include "formats/math3d.h"
#include "spots.h"

class Resources;

// The shadow under the player's car (RCCars.exe 0x5072f0, with VIDEO_CarShadow on): one textured spot of the light
// queue (kind 3, 0x44c640) laid onto the track, drawn darkening what is under it. Settings\carRender.ini gives each
// car its ShadowSize<n> (radius, in 1/100 m) and ShadowShift<n> (where along the car the spot sits: shift/100 - 0.5
// m forwards of its centre); the texture is RaceTextures shadows2 for the Overkill, shadows3 for the Buggy and
// shadows1 for the Hummer. ShadowDensity, FilterSpeed, TextureSizePow2 and FrameSkipNmb are read but used by
// nothing. The opponents have no shadow in the original.
class CarShadow {
  public:
    explicit CarShadow(Resources& res);

    // the spot of car `car` (0..2: Overkill, Buggy, Hummer) at `pose`
    SpotView::Spot spot(size_t car, const rc::Mat4& pose) const;

  private:
    struct Look {
      float                     size  = 0.36f;
      float                     shift = 0.0f;
      const Tempest::Texture2d* texture = nullptr;
      };
    Look looks[3];
  };
