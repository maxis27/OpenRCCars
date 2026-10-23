#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "formats/collision.h"
#include "formats/settings.h"
#include "particles.h"
#include "tracks.h"
#include "wake.h"

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
  // Settings/exhausted_gas.ini (0x5308f0): smoke from the exhaust of the boosters bought
  struct Exhaust {
    float gravityY = 0;
    float dx = 0, dy = 0;
    float twist = 0;
    float scaleX = 0, scaleXDisp = 0, scaleY = 0, scaleYDisp = 0;
    float grow = 1;                     // DynamicScale: size grows by this - 1 every second, without end
    float speed = 0;                    // m/s out of the exhaust, the car's own speed added
    float speedAtt = 1;                 // horizontal speed left after 1 s
    float life = 0, lifeDisp = 0;
    float explodeTime = 0;              // s of the puff when the turbo comes on
    float explodeColor = 1;             // grey it darkens to (0..1)
    float explodeAlpha = 1;             // opacity of its smoke (0..1)
    float wheelSpin = 0;                // rear wheel slip above which a spinning wheel makes smoke
    CurveSpline intensity;              // Splines/eg_int.spl: particles a second, by km/h
    } exhaust;
  TrackSettings tracks;                 // Settings/car_trace.ini
  WakeSettings  wake;                   // Settings/wtr_trace.crs
  FoamSettings  foam;                   // Settings/whr_horse.ini

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
    float                slip = 0;         // 0..1: wheel spin (RCCars.exe 0x5019a0, Vehicle::wheelSlip), with
    float                rimSpeed = 0;     // rad/s the wheel turns at, + forwards; the computer's replays: both 0
    int                  slot = -1;        // RCCars.exe's number: 0/1 front left/right, 2/3 rear, 4/5 the middle
    Vec3                 axle{1,0,0};      // world, turned with the steering
    Vec3                 normal{0,1,0};    // of the ground at `contact`
    uint32_t             triangle = 0;     // the ground's CollisionMesh triangle
    uint8_t              sound = 0;        // the ground's SoundSurface
    };
  struct Exhaust {
    Vec3 pos, dir;                         // world
    };
  Mat4               pose;                 // model -> world (+Z forward)
  Vec3               velocity;             // m/s
  bool               gas = false, brake = false;
  bool               boost = false;        // the turbo is on
  int                upgrade = 0;          // 0..3: the tyres and the booster bought
  std::vector<Wheel> wheels;
  std::optional<Exhaust> exhaust;          // the booster's end, if the model has it
  Vec3               body;                 // middle of the body, world
  float              bodyHeight = 0;       // m
  std::optional<float> bodyWater;          // the water surface near the body

  // fills in each wheel's ground contact, surface and water, and the body's water, from the track, given hub,
  // radius, front and left: the same for the player's car and the computer's replays, whatever drives them
  void sense(const CollisionMesh& track);
  };

// The effects RCCars.exe gives a car (0x531650): dust behind the wheels (car_dustx, 0x52e000) and spray where they
// cut the water (WATER_SPRAY, 0x527e20), tyre tracks (car_trace, 0x52f050), and for the players' cars the exhaust
// smoke (exhausted_gas, 0x530180), the wake and foam in water (wtr_trace 0x528ce0, whr_horse 0x526020) and the dirt
// the dust leaves on the body (0x52e320, 0x531c20). The players' cars have them on every wheel, the computer's cars
// only on the rear wheels and with dust living half as long.
class CarEffects {
  public:
    CarEffects(const EffectSettings& settings, bool player);

    // after the physics; `camera`: dust starts only within 12 m of it
    void update(const CarState& car, const Vec3& camera, float dt, Random& rnd);
    void collect(std::vector<Billboard>& out) const;
    // tyre tracks within 20 m of the camera, the wake and the foam
    void collect(std::vector<Mark>& out, const Vec3& camera) const;
    // all of it gone (a new race); with `keep` only the particles, the tracks end where they are and the dirt stays
    // (the car put back on the track)
    void clear(bool keep = false);
    // how dirty the body is: 0 clean .. 2 the dirtiest texture (0x531c20 blends dirt_car<n>_1 and _2)
    float dirt() const;

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
    void smoke(const CarState& car, float dt, Random& rnd);
    void marks(const CarState& car, float dt, Random& rnd);

    const EffectSettings& set;
    bool                  player = false;
    std::vector<WheelFx>  wheels;
    TyreTracks            tracks;
    Wake                  wake;
    // exhaust (0x5303c0): the puff when the turbo comes on, the smoke's grey, the emitter
    Emitter               exhaust{100};
    float                 explode = 0;      // s of the puff left
    bool                  boostWas = false;
    float                 grey = 255;
    bool                  puff = false, boostSmoke = false;
    // dirt: grows with the dust the wheels raise, washed off in water, up to 3000
    float                 dirtAmount = 0;
  };

}
