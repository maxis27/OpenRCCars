#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "formats/collision.h"
#include "formats/water.h"
#include "marks.h"
#include "particles.h"

namespace rc::fx {

// Settings of the track's own effects, read as RCCars.exe reads them: each slider is its stored position over a
// divisor written in the code (0x532410), unless noted.
struct WorldSettings {
  // WATER_SPLASH (water_splash.crs, 0x5272f0) and BULLET_EXPLODE (bullet_explode.crs, 0x52cd30): a short burst of
  // drops
  struct Drops {
    float rgb[3]    = {255,255,255};
    float dx = 0, dy = 0;             // sprite size, see Billboard
    float life = 0, lifeDisp = 0;     // s
    float angleSpan = 45;             // degrees: the splash's fan, the bullet's cone
    float velLen = 0, velLenDisp = 0; // m/s
    float scale = 1, timeScale = 0;   // the size goes from 1 to `scale` in `timeScale` s
    float intensity = 0;              // drops a second ..
    float emitLife = 0;               // .. for this many seconds
    float splashLen = 0;              // splash: m, how far apart the drops start
    float velAtt = 1;                 // splash: what is left of the speed at the end of the life
    float alphaAtt = 0;               // share of the life before the drop starts to fade out
    } splash, explode;
  // WATER_SPLASH_MNG (water_splash_mng.ini, 0x527d80): the splashes at the water_splash_N markers
  struct SplashManager {
    float delay = 1, delayDisp = 0.25f;   // s between two splashes of a marker
    float radius = 0.05f;                 // m from the marker
    float activeRad = 20;                 // m: only markers this near the camera
    } splashes;
  // BULLET_DUST (bullet_dust.crs, 0x52c7a0) and BULLET_SMOKE (bullet_smoke.crs, 0x52d2d0): puffs of dust where a
  // bullet hits, smoke from a car it hit
  struct Puff {
    float rgba[4] = {255,255,255,255};
    float dx = 0, dy = 0;
    float intensity = 0, timeEmit = 0;  // puffs a second for timeEmit s
    float life = 0;                     // TimePart
    float speedY = 0;                   // m/s up
    float speedDecay = 1;               // the speed is multiplied by this every frame
    float twist = 1;
    float scaleX = 1, scaleXDisp = 0, scaleY = 1, scaleYDisp = 0;
    float grow = 1, growTime = 1;       // the size grows `grow` times over growTime of the life
    } bulletDust, bulletSmoke;
  // BURST_MNG (burst_mng.ini, 0x52df20): a guard's gun
  struct Burst {
    int   shots = 6;            // NShoots, a shot every timeShoot s
    float timeShoot = 0.1f;
    float offset = 0;           // the aim leads the car by speed x offset / 2
    float radius = 0;           // m: the shots sweep across the car this far
    } burst;
  // WaterLOD_Wave (waterLOD_wave.ini, whole numbers, 0x525c10) and Water_Wave_New (water_wave_new.ini, 0x52a550):
  // the waves breaking at the water_wave_N markers
  struct Waves {
    float timeLong = 2, timeShort = 0.5f;   // s between two long / short waves of a marker
    float angle = 5;                        // degrees: a long wave turns this far at random
    float len = 3.5f;                       // m: short waves come up this far around the marker
    float activeRad = 40;                   // m
    float lifeLong = 3.5f, lifeShort = 3.6f, lifeShortDisp = 0.25f;
    float halfLen = 1.7f;                   // m: half the width of a wave
    float height = 0.58f;                   // m
    float dHeight = -0.2f;                  // DHeight - 1: how far the wave sinks along the water's normal
    float animSpeed = 1.86f, animWait = 0.1f; // its texture moves animSpeed a second, in steps of animWait s
    float incTime = 0.49f, decTime = 0.55f; // shares of the life: fading in until incTime, out from decTime
    } waves;

  using Reader = std::function<std::vector<uint8_t>(const std::string&)>;
  // `read` gets files from RCCars.pack; throws if a file is missing
  static WorldSettings load(const Reader& read);
  };

// Where the effects are seen from.
struct View {
  Vec3 eye;
  Mat4 viewProj;
  // in front of the camera and inside the picture (the markers' test, 0x527c40 / 0x525a40)
  bool onScreen(const Vec3& p) const;
  };

// The splashes of waves on the rocks at a track's water_splash_N markers (RCCars.exe 0x5277a0 .. 0x527a40): a
// marker near the camera and on the screen splashes every TimeDelay +- TimeDelayDisp s somewhere within Radius m
// of it: a fan of drops thrown up in the plane facing the camera. Its sound is AmbientSounds' water_splash.
class WaterSplashes {
  public:
    WaterSplashes() = default;
    WaterSplashes(const WorldSettings& settings, const std::vector<Vec3>& markers);

    void update(const View& view, float dt, Random& rnd);
    void collect(std::vector<Billboard>& out) const;
    void clear();

  private:
    struct Marker {
      Vec3  pos;
      bool  active = false;
      float timer  = 0;
      };
    struct Splash {
      Emitter drops{128};
      Vec3    at;
      float   emit = -1;      // s still to make drops, < 0: free
      };

    const WorldSettings*  set = nullptr;
    std::vector<Marker>   markers;
    std::vector<Splash>   pool;      // two a marker (0x527930)
  };

// The waves breaking at a track's water_wave_N markers (RCCars.exe 0x5255c0 .. 0x525700, Water_Wave_New
// 0x529c60 .. 0x52a030): every TimeLong s a long wave comes up on the crest of the sea's wave 2 just beyond the
// marker, every TimeShort s a short one somewhere around it; both ride wave 2 in to the shore and fade. Each wave is
// a quad standing on its crest, turned to the camera around the crest line, its texture (water_wave) running along
// it and its edges faded by water_wave_alpha.
class ShoreWaves {
  public:
    // the water surface: its height at a point (with the waves of the time given to update), if there is water
    using Water = std::function<std::optional<float>(const Vec3&)>;

    ShoreWaves() = default;
    // `sea`: the sea the waves ride (the node "more1", else the first one)
    ShoreWaves(const WorldSettings& settings, const std::vector<Vec3>& markers, const WaterWaves& sea);

    // `time`: the water animation's time
    void update(const View& view, float time, float dt, const Water& water, Random& rnd);
    void collect(std::vector<Mark>& out, const Vec3& eye) const;
    void clear();

  private:
    struct Marker {
      Vec3  pos;
      float timeLong = 0, timeShort = 0;
      };
    struct Wave {
      bool  live = false;
      float life = 0, left = 0;
      float frame = 0;           // texture offset along the crest
      float anim = 0;            // s since the last step of the texture
      float fade = 0;            // 0..1
      float travelled = 0;       // s
      Vec3  start, heading;      // where it came up; away from the sea's centre
      Vec3  pos, normal{0,1,0};  // now, on the water
      Vec3  side;                // along the crest
      };

    bool spawn(const Vec3& at, const Vec3& heading, bool longWave, Random& rnd);
    bool place(Wave& w, const Water& water) const;

    const WorldSettings*  set = nullptr;
    WaterWaves            sea;
    std::vector<Marker>   markers;
    std::vector<Wave>     pool = std::vector<Wave>(32);   // 0x165ade8: 32 waves
  };

// A guard shooting at the player's car (RCCars.exe BURST_MNG, 0x52d570 .. 0x52df20): shots TimeShoot apart sweep
// across the car; a bullet that hits the ground throws up a cone of grit (bullet_explode) and a puff of dust
// (bullet_dust) and leaves a hole (bullet_footprint, 8 s); one that hits the car makes it smoke (bullet_smoke).
class Gunfire {
  public:
    // the player's car, as the bullets see it
    struct Target {
      Mat4 pose;              // model -> world
      Vec3 velocity;          // m/s
      Vec3 boxMin, boxMax;    // its body, model space
      };
    // what the game should play: the gun (burst) at a guard, a bullet (bullet) where it hit
    struct Sound {
      enum Kind : uint8_t { Burst, Bullet } kind;
      Vec3 at;
      };

    Gunfire() = default;
    explicit Gunfire(const WorldSettings& settings);

    // `guns`: the muzzles shooting now (Characters::shooting); `car` null: nobody to shoot at
    void update(const std::vector<Vec3>& guns, const Target* car, const CollisionMesh& track, float dt,
                Random& rnd);
    void collect(std::vector<Billboard>& out) const;
    void collect(std::vector<Mark>& out) const;
    // the sounds since the last call
    std::vector<Sound> sounds();
    void clear();

  private:
    struct Puffs {
      Emitter puffs{64};
      float   emit = -1;
      };
    struct Grit {
      Emitter drops{64};
      float   emit = -1;
      Vec3    at;
      };
    struct Hole {
      Mark  quad;
      float left = 0;
      };

    void shoot(const Target& car, const CollisionMesh& track, Random& rnd);
    void puffs(Puffs& p, const WorldSettings::Puff& s, const Vec3& at, float dt, Random& rnd);

    const WorldSettings*  set = nullptr;
    // the burst going on (0x52de30): its gun, the sweep's ends, the time left, the last shot
    Vec3                  gun;
    Vec3                  sweepA, sweepB;
    float                 timer = -1;
    int                   shot  = -1;
    std::vector<Grit>     grit  = std::vector<Grit>(8);    // 8 of each (0x52d8b3)
    std::vector<Puffs>    dust  = std::vector<Puffs>(8);
    std::vector<Vec3>     dustAt = std::vector<Vec3>(8);
    Puffs                 smoke;                           // the car's (0x52cf90)
    std::vector<Hole>     holes;                           // at most 256 (0x478440)
    std::vector<Sound>    heard;
  };

}
