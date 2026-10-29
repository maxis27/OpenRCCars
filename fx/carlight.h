#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "formats/collision.h"
#include "formats/csi.h"
#include "formats/math3d.h"
#include "formats/scene.h"
#include "formats/settings.h"

namespace rc {
class PackFile;
}

namespace rc::fx {

// How RCCars.exe lights the cars (Settings\carLight.ini, read by 0x5077e0 for every LOD drawn): the nearest LOD
// (0) with Direct3D's light, an ambient AmbientI and one directional light DirectI (0..255), both times the car's
// brightness (CarShade) and held at 255; the far LODs with the light off, their textures as they are. The light
// comes from (0,0,1) turned by AngleAzimuth about the up axis, then by AngleElevation up towards (0,1,0) (0x40b4c0
// turns a vector towards another by an angle): (0, 0.966, 0.259) with the shipped 0 and 75 degrees. The colour
// keys (AmbientR .. DirectB) are read by nothing.
// The brightness follows the lightmap under the car (Settings\car_lmshd.ini, 0x531ff0, every frame for every car):
// while a camera is within maxdist (25 m) of the car and VIDEO_Light or VIDEO_EnvMap is on, it averages the
// lightmap's texels under the (first four) wheels touching the track, or with none of them, for the player's car
// only, the one under the car (a ray 1.5 m down from 1 cm above it). The texel's (r+g+b)/3 times 255 goes through
// Splines\light_car.bspl (linear, kept at its end values; 112.6 -> 158.9 and 200.9 -> 260.0, so 0.62 .. 1.02)
// over 255. The first time the brightness is that, from then on it moves at alphavel/100 (1.27) a second towards
// it once it is more than deltaalpha/100 (0.05) off. A car no camera is near has 1.
struct CarLight {
  float ambient = 138.f/255.f;   // 0..1, before the brightness
  float direct  = 128.f/255.f;
  Vec3  toLight = {0,0.9659258f,0.2588190f};
  // car_lmshd.ini
  float  alphaVel   = 1.27f;    // 1/s
  float  deltaAlpha = 0.05f;
  float  maxDist    = 25.f;     // m
  Spline curve;                 // light_car.bspl, lightmap 0..255 -> brightness 0..255
  Spline featCurve;             // light_feat.bspl, the same for FeatureShade

  static CarLight load(PackFile& pack);
  };

// The lightmap texel under a point of the track (RCCars.exe 0x4572c0): the face's LM_1 uvs at the point,
// interpolated from its first three corners, and the texel there (no filtering), (r+g+b)/3 in 0..1.
class LightmapProbe {
  public:
    // `read` gives a pack file's bytes (throws if it is missing)
    using Read = std::function<std::vector<uint8_t>(const std::string&)>;
    LightmapProbe(const SbScene& scene, const CollisionMesh& track, Read read);

    // at `point` on track triangle `triangle`; none for a face without a lightmap
    std::optional<float> at(uint32_t triangle, const Vec3& point);
    // on the solid track at most `length` under `from`
    std::optional<float> below(const Vec3& from, float length);
    // the same for the people, guards, props and road cars (0x532d80): 1 on a face without a lightmap, none only
    // when nothing is there
    std::optional<float> ground(const Vec3& from, float length);

  private:
    const Image* texture(int32_t index, int32_t model);

    const SbScene&       scene;
    const CollisionMesh& track;
    Read                 read;
    std::map<std::pair<int32_t,int32_t>,Image> images;   // by texture and model; empty if it does not load
  };

// one car's brightness (0x531ff0, car data 0xe824, 0xe820 whether it was set)
class CarShade {
  public:
    // `near`: a camera is within CarLight::maxDist; `light`: the average lightmap value under it (0..1), none if
    // nothing was found
    void  update(const CarLight& cfg, float dt, bool near, std::optional<float> light);
    float value() const { return k; }
    void  reset() { k = 1; primed = false; }

  private:
    float k      = 1;
    bool  primed = false;
  };

// The brightness of the people, guards, props and road cars (RCCars.exe 0x532cf0 sets it up, 0x532d50 its
// settings, 0x532d80 updates it; with VIDEO_CharLight, on by default): what the lightmap under them says, through
// Splines\light_feat.bspl (the same curve as light_car.bspl; Settings\feat_lmshd.ini only names it, the game asks
// for "light_feat" itself) over 255, held at 0..1, so 0.62 in the shade and 1 in the sun. It goes to Direct3D's
// texture factor of all their meshes (0x451390 -> node +0x2fc, flag 0x100) and the texture is multiplied by it
// (0x464d08, 0x45c980: MODULATE TEXTURE, TFACTOR), with no other light: in the sun they show their textures as they
// are. The lightmap is looked up 0.1 m above where they stand and down to 2 m below; nothing there leaves the
// brightness as it was (a face without a lightmap gives 1). Every `interval` s (negative: every update) it takes
// the value under them as its target, then moves at `speed` a second towards it while it is more than `delta` off.
class FeatureShade {
  public:
    struct Settings {
      float speed    = 2;     // /s
      float delta    = 0.1f;
      float interval = -1;    // s
      };
    // the props of stone.sb (STON 0x50cb00; the original takes the ground the prop's body touches, we look below it)
    static constexpr Settings Prop   = {2,0.1f,-1};
    // the people (PEOP 0x510400), while they are about
    static constexpr Settings Person = {2,0.1f,1};
    // the guards (GUAM 0x51a7d0): once, when they are set up
    static constexpr Settings Guard  = {0,0,-1};
    // the road cars (RDCR 0x516f50) take `delta` from roadcar.ini DeltaAlpha/100 (0.14); their head lights stay
    // white (0x2fc = 0xffffffff)

    // what is under them: the lightmap (0..1, 1 without one), none when nothing is there
    using Sample = std::function<std::optional<float>()>;
    // `dt` <= 0 puts it at its target right away; until the first look it is 1
    void  update(const Spline& curve, const Settings& cfg, float dt, const Sample& sample);
    float value() const { return k; }

  private:
    float target = 1, k = 1, timer = 0;
  };

}

