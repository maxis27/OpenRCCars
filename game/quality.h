#pragma once

#include <cstdint>

#include "formats/settings.h"

// The options' "Jakość grafiki" and "Jakość efektów" (RCCars.crs [Video] RenderQual, SFXQual: 0 low, 1 medium,
// 2 high). RCCars.exe reads the two at the start and whenever the video options are left (0x4a2635, 0x4c854b) and
// turns them into the VIDEO_* switches the rest of the game reads (0x4acdb0); a value outside 0..2 switches all of
// its own on (non-zero) or off. A few places read RenderQual and SFXQual themselves.
struct Quality {
  int32_t render = 2;
  int32_t sfx    = 2;

  // RenderQual medium and high
  bool lightMaps  = true;   // VIDEO_LightMaps: the track's lightmaps (0x48fb10), the light under the cars (0x4572c0)
  bool light      = true;   // VIDEO_Light: the cars lit by carLight.ini (0x5077e0)
  bool envMap     = true;   // VIDEO_EnvMap: the reflections on the cars and the sea (0x507549, 0x507ba0)
  bool waterCoast = true;   // VIDEO_WaterCoast: the foam along the shore (0x51c974)
  // RenderQual high
  bool charLight  = true;   // VIDEO_CharLight: people, props and road cars shaded by the lightmap (0x532d80)
  bool detailer   = true;   // VIDEO_Detailer: the detail textures (0x480e50)
  // SFXQual medium and high
  bool carShadow     = true; // VIDEO_CarShadow: the player's car's shadow (0x5072f0)
  bool dust          = true; // VIDEO_DustEff: dust behind the wheels (0x52e000)
  bool exhaust       = true; // VIDEO_ExhaustGas: the exhaust smoke (0x530180)
  bool waterTrace    = true; // VIDEO_WaterTrace: spray, wake and foam of the cars in water (0x527e20, 0x528ce0, 0x526020)
  bool burst         = true; // VIDEO_Burst: smoke and grit where the gunfire hits (0x52cf90, 0x52d874)
  bool roadCarShadow = true; // VIDEO_RoadCarShadow: the road cars' wheel shadows and head light glow (0x5171d6)
  bool stream        = true; // VIDEO_Stream: the splashes at the water_splash_N markers (0x5277a0)
  // SFXQual high (VIDEO_DirtCarEff too, which nothing reads: the cars get dirty anyway)
  bool hatchSmoke    = true; // VIDEO_HatchSmokeEff: the coolers' smoke (0x52b750)
  bool peopSole      = true; // VIDEO_PeopSole: the people's footprints (0x50f7f0)
  bool wheelTrace    = true; // VIDEO_WheelTrace: the tyre tracks (0x52f050)

  static Quality from(int32_t render, int32_t sfx) {
    Quality q;
    q.render = render;
    q.sfx    = sfx;
    // 0x4acdb0: 0 both off, 1 the first on, 2 both on, anything else the value itself
    auto split = [](int32_t v, bool& medium, bool& high) {
      medium = v!=0;
      high   = v!=0 && v!=1;
      };
    bool m = true, h = true;
    split(render,m,h);
    q.lightMaps = q.light = q.envMap = q.waterCoast = m;
    q.charLight = q.detailer = h;
    split(sfx,m,h);
    q.carShadow = q.dust = q.exhaust = q.waterTrace = q.burst = q.roadCarShadow = q.stream = m;
    q.hatchSmoke = q.peopSole = q.wheelTrace = h;
    return q;
    }
  static Quality load(const rc::ConfigFile& crs) {
    // the defaults of 0x4e26e6: both high
    return from(crs.integer("Video","RenderQual",2),crs.integer("Video","SFXQual",2));
    }

  // what reads RenderQual itself treats it as 0, 1 or 2
  int32_t renderLevel() const { return render<0 || render>2 ? 2 : render; }
  int32_t sfxLevel()    const { return sfx<0 || sfx>2 ? 2 : sfx; }
  };
