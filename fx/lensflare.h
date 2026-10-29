#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "formats/math3d.h"

namespace rc::fx {

// The sun and its lens flare (RCCars.exe class SUN$ 0x478cd0, VIDEO_Sun; drawn on render message 0x68 by 0x479070,
// 0x479250 and 0x4797c0). The sun stands at the track's marker SUN_AF (0x478fd0; on the sky dome, the urban
// tracks have none):
// - the disc (0x479070): the texture sun_disc in ColorR/G/BSun, alpha blended (0x4791e0, render flags 0xa0000003),
//   at the sun's depth, so what stands in front of it hides it; vpW x SizeSun pixels across and vpW/(aspect x 0.75)
//   high, where the aspect is the constant 4/3 (0x48f520), so square on any screen;
// - the flare (0x479250): every TimeShootRay s a ray from the camera to the sun (0x456260) says whether it can be
//   seen; it fades in or out over FadeTime. Its pieces lie on the line from the point C = 0.1 m + CamOffset in front
//   of the camera (the view plane, 0x500760) towards the sun: C + dir x Dist1..4 (Dist1 and Dist2 behind C, so
//   they fall on the other side of the screen's middle than the sun), the fifth (shine) on the sun itself. Each is
//   a square vpW x Size pixels across, added onto the picture (0x4799a0, render flags 0xa0200005: SrcAlpha, One)
//   in front of everything. Their opacity is 255 x (1 - offset/(half the screen + half the disc)) of the sun's
//   offset from the middle, the smaller of across and up, times the fade; at 0 the flare is off and fades in
//   again from nothing.
class LensFlare {
  public:
    struct Settings {               // Settings\LENS_FLARE.ini (0x479b70): /100 but the colours
      float timeShootRay = 0.5f;
      float camOffset    = 0;
      float fadeTime     = 0.25f;
      float dist[4]      = {-0.05f,-0.02f,0.04f,0.11f};
      float size[5]      = {0.45f,0.03f,0.10f,0.02f,0.50f};
      float rgb1[3]      = {200,255,201};
      float rgb3[3]      = {171,255,170};
      float sizeSun      = 0.05f;
      float rgbSun[3]    = {255,255,188};
      using Reader = std::function<std::vector<uint8_t>(const std::string&)>;
      static Settings load(const Reader& read);
      };

    // what to draw, in pixels (y down), as Effects' sprites
    struct Quad {
      enum Texture : uint8_t { SunDisc, Flare1, Flare2, Flare3, Shine } texture = SunDisc;
      float x = 0, y = 0;           // the middle
      float halfW = 0, halfH = 0;
      float rgba[4] = {1,1,1,1};
      float depth = 0;              // the disc's, tested against the scene; the flare is in front of all
      bool  additive = false;
      };

    // something between the two points hides the sun
    using Blocked = std::function<bool(const Vec3& from, const Vec3& to)>;

    LensFlare() = default;
    LensFlare(const Settings& settings, std::optional<Vec3> sun);

    bool has() const { return sun.has_value(); }
    // `eye`, `forward`: the camera; `viewProj` its matrix; `w` x `h` the screen in pixels
    void update(float dt, const Vec3& eye, const Vec3& forward, const Mat4& viewProj, int w, int h,
                const Blocked& blocked);
    const std::vector<Quad>& quads() const { return out; }
    void clear();

  private:
    enum State : uint8_t { Off = 0, FadingIn = 1, FadingOut = 2, On = 3 };

    Settings             set;
    std::optional<Vec3>  sun;
    State                state = Off;
    float                fade  = 0;     // s of the fade left (0x17c)
    float                shoot = 0;     // s to the next ray (0x184)
    bool                 seen  = false; // the last ray (0x180)
    std::vector<Quad>    out;
  };

}
