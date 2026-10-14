#pragma once

#include <cstdint>
#include <string_view>

namespace rc {

class PackFile;
class SliderFile;

// Fog of a track (RCCars.exe 0x5324c0, "Fog" module). On loading a track the game reads Settings\fog_<track>.ini
// (0x532870):
//   Start, End     - slider positions as they are (unused: the fog is exponential)
//   Density        - value * 0.001
//   ColorR/G/B     - 0..255
// and fades to it (0x532770 with no object). Direct3D draws it as D3DFOG_EXP table fog (0x461970): a pixel
// `depth` metres in front of the camera keeps exp(-density*depth) of its colour, the rest is the fog colour.
// Meshes drawn with the environment map (the sea, the car bodies) turn the fog off (0x464e50 .. 0x4658b0).
// No fog_<track>.ini: start 0, end 1, density 0, colour white.
//
// Every change fades in 0.5 s (0x532aa0): t grows by the frame time and each value goes linearly from what was
// drawn when the change began to the new one, by 2*t. An object's FOG section (FOG_Start, FOG_End, FOG_EndInt as
// the density, FOG_Color) could switch to other fog on game events 126 (enter) and 127 (back to the track's), but
// no shipped track has such an object.
struct FogParams {
  uint32_t color   = 0;   // 0xffRRGGBB
  float    start   = 0;
  float    end     = 0;
  float    density = 0;

  static FogParams parse(const SliderFile& f);
  // Settings\fog_<track>.ini, or the "no file" values above
  static FogParams load(PackFile& pack, std::string_view track);
  };

class Fog {
  public:
    static constexpr float Fade = 0.5f; // seconds

    // fade from what is drawn now to `p`
    void setTarget(const FogParams& p);
    void tick(float dt);

    // what to draw this frame; the colour is 0x00RRGGBB once a fade has run (the fade drops the alpha)
    const FogParams& current() const { return now; }
    const FogParams& target()  const { return to; }
    // how much of a pixel's colour is left `depth` metres in front of the camera
    static float     visibility(float density, float depth);

  private:
    FogParams now;   // the game starts with everything 0: no fog
    FogParams from;
    FogParams to;
    float     t = Fade;
  };

}
