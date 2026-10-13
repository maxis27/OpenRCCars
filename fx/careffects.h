#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "formats/collision.h"
#include "formats/settings.h"
#include "particles.h"

namespace rc::fx {

// Settings of the wheel effects, as RCCars.exe reads them: every slider is its stored position over a divisor
// written in the code (0x532410), not mapped onto a range like the car's physics sliders.
struct EffectSettings {
  // Settings/car_dustx.ini (0x52ea60) and surf_<surface>.crs (0x52ed30): dust behind the wheels
  struct Surface {
    float intScale = 0;          // emission x this, 0: no dust on this surface
    float rgba[4]  = {1,1,1,1};  // 0..255
    float life = 0, lifeDisp = 0;
    };
  struct Dust {
    float dx = 0, dy = 0;               // sprite size, see Billboard and CarEffects
    float twist = 0;
    float scaleX = 0, scaleXDisp = 0, scaleY = 0, scaleYDisp = 0;
    float grow = 1, growFront = 1;      // DynamicScale / DynamicScaleForw: size after 1 s, rear / front wheels
    float speedAtt = 1;                 // horizontal speed left after 1 s
    float spinThresh = 0, spinScale = 0, spinSpeed = 0;
    float shiftX = 0;                   // front wheels: moved towards each other by this share of the track
    float gravityY = 0;
    CurveSpline intensity;              // Splines/dust_int.spl: particles a second from the speed in km/h
    CurveSpline speed;                  // Splines/dust_speed.spl: % of the car's speed the dust takes along
    std::array<Surface,size_t(rc::Surface::Water)+1> surfaces; // by rc::Surface
    } dust;
  // Settings/water_spray.crs (0x5289f0): spray off the wheels in water
  struct Spray {
    float dx = 0, dy = 0;
    float rgba[4] = {1,1,1,1};
    float life = 0, lifeDisp = 0;
    float shiftX = 0;
    float angle = 0, angleDisp = 0, angleSpan = 45; // degrees
    float velLen = 0, velLenDisp = 0;
    float scale = 1, timeScale = 0;
    float oscillation = 0, speedDecay = 0;
    CurveSpline intensity;              // Splines/wspr_int.spl: particles a second from the speed in km/h
    } spray;

  using Reader = std::function<std::vector<uint8_t>(const std::string&)>;
  // `read` gets files from RCCars.pack ("Settings\\car_dustx.ini"); throws if it cannot
  static EffectSettings load(const Reader& read);
  };

// What the effects need to know about a car each frame.
struct CarState {
  struct Wheel {
    Vec3                 hub;              // world
    float                radius = 0;
    bool                 front  = false;
    bool                 left   = false;   // +X side of the model
    bool                 onGround = false;
    Vec3                 contact;          // where it touches the ground
    rc::Surface          surface = rc::Surface::Default;
    std::optional<float> water;            // height of the water surface over the wheel, if any
    float                slip = 0;         // 0..1: wheel spin (RCCars.exe 0x5019a0), with
    float                rimSpeed = 0;     // m/s of the tyre's surface; our Bullet car has no wheel spin: both 0
    };
  Mat4               pose;                 // model -> world (+Z forward)
  Vec3               velocity;             // m/s
  bool               gas = false, brake = false;
  std::vector<Wheel> wheels;

  // fills in each wheel's ground contact, surface and water from the track, given hub, radius, front and left:
  // the same for the player's car and the computer's replays, whatever drives them
  void sense(const CollisionMesh& track);
  };

// The effects RCCars.exe gives a car (0x531650): dust behind the wheels (car_dustx, 0x52e000) and spray where they
// cut the water (WATER_SPRAY, 0x527e20). The players' cars have them on every wheel, the computer's cars only on the
// rear wheels and with dust living half as long.
class CarEffects {
  public:
    CarEffects(const EffectSettings& settings, bool player);

    // after the physics; `camera`: dust starts only within 12 m of it
    void update(const CarState& car, const Vec3& camera, float dt, Random& rnd);
    void collect(std::vector<Billboard>& out) const;
    void clear();

  private:
    struct WheelFx {
      Emitter dust{300}, spray{200};
      // dust
      float   rate = 0;
      Vec3    velocity;          // the particles take this along, scaled by dust_speed.spl
      const EffectSettings::Surface* surface = nullptr;
      float   alpha = 0;
      bool    front = false;
      // spray
      Vec3    forward;           // horizontal heading of the car
      float   chord = 0;         // half the width of the wheel at the water line
      };
    void dust (WheelFx& fx, size_t i, const CarState& car, const Vec3& camera, float dt, Random& rnd);
    void spray(WheelFx& fx, const CarState::Wheel& w, const CarState& car, float dt, Random& rnd);

    const EffectSettings& set;
    bool                  player = false;
    std::vector<WheelFx>  wheels;
  };

}
