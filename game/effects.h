#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/RenderPipeline>
#include <Tempest/Texture2d>
#include <Tempest/VertexBuffer>

#include <memory>
#include <vector>

#include "fx/careffects.h"
#include "fx/worldfx.h"
#include "quality.h"

class Resources;

namespace rc {
class CollisionMesh;
struct SbScene;
struct WaterWaves;
class Opponents;
class Vehicle;
}

// The cars' effects (fx/careffects.h), for the player's car and the opponents: dust behind the wheels, spray in water
// and exhaust smoke, and the track's own (fx/worldfx.h): splashes, breaking waves, gunfire; drawn as sprites facing
// the camera, sorted far to near; tyre tracks, the wake, the foam, the waves and the bullet holes as quads in the
// world: the tracks before the translucent pass of the track, the rest after it. All tested against the
// scene's depth without writing it.
class Effects {
  public:
    Effects(Tempest::Device& device, Resources& res);

    // all gone (a new race); with `keep` (the car put back) the particles only: the tracks stay, and the dirt
    void clear(bool keep = false);
    // how dirty the player's car is: 0 clean .. 2 (CarEffects::dirt); `p` 1: player 2's of the split screen
    float dirt(size_t p = 0) const;
    // the split screen's player 2: its car's effects as the player's, and only its particles gone (put back)
    void tickSecond(float dt, const rc::Vehicle& car, const rc::CollisionMesh& track, const rc::Vec3& camera, bool gas,
                    bool brake);
    void clearSecond(bool keep);
    // what the options' quality leaves (game/quality.h): of the cars' effects and of the track's
    void setQuality(const Quality& q);
    // the track's own effects (fx/worldfx.h): splashes at its water_splash_N markers, waves breaking at its
    // water_wave_N markers on `sea` (the main sea's waves, null: none), the guards' gunfire, the smoke out of its
    // coolers (*KOOLER* nodes)
    void setTrack(const rc::SbScene& scene, const rc::WaterWaves* sea);
    // after the physics; `gas`/`brake`: the player's keys
    void tick(float dt, const rc::Vehicle& player, const rc::Opponents* opponents, const rc::CollisionMesh& track,
              const rc::Vec3& camera, bool gas, bool brake);
    // the track's effects, seen through `view`; `time`: the water's animation, `water` its surface; `bursts`: the
    // guards and the BTR shooting (Characters::shooting, Traffic::shots) at `cars`; `walkers`: the characters,
    // for their footprints
    void tickWorld(float dt, const rc::fx::View& view, float time, const rc::fx::ShoreWaves::Water& water,
                   const std::vector<rc::fx::Gunfire::Order>& bursts,
                   const std::vector<rc::fx::Gunfire::Target>& cars, const rc::CollisionMesh& track,
                   const std::vector<rc::fx::Footprints::Walker>& walkers);
    // people's footprints on the ground now
    size_t footprintCount() const { return footprints.size(); }
    // puffs of the coolers' smoke now
    size_t smokeCount() const { return hatch.size(); }
    // the gunfire's sounds and the cars it hit since the last call
    std::vector<rc::fx::Gunfire::Sound> gunSounds() { return gunfire.sounds(); }
    std::vector<rc::fx::Gunfire::Hit>   gunHits()   { return gunfire.hits(); }
    // the track's fog (formats/fog.h) for what is drawn next: density and 0x00RRGGBB
    void setFog(float density, uint32_t color) { fogDensity = density; fogColor = color; }
    // the tyre tracks, after the opaque scene and before the translucent pass (water over them hides them)
    void drawTracks(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, const rc::Mat4& viewProj,
                    const rc::Vec3& eye);
    // the wake, the foam and the particles; `eye`: the camera's position
    void draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, int w, int h, const rc::Mat4& viewProj,
              const rc::Vec3& eye);

  private:
    struct Vertex {
      float pos[2];
      float uv[2];
      float color[4];
      float depth = 0;
      float fog   = 1;   // how much of the colour the fog leaves
      };
    struct MarkVertex {
      float pos[3];
      float uv[2];
      float uv2[2];
      float color[4];
      };
    struct Opponent {
      std::unique_ptr<rc::fx::CarEffects> fx;
      rc::Vec3                            last;
      bool                                placed = false;
      };

    Tempest::Device&                    device;
    rc::fx::EffectSettings              settings;
    bool                                loaded = false;
    rc::fx::Random                      rnd;
    std::unique_ptr<rc::fx::CarEffects> player;
    std::unique_ptr<rc::fx::CarEffects> player2;   // the split screen's
    float                               fogDensity = 0;
    uint32_t                            fogColor   = 0;
    std::vector<Opponent>               opponents;
    rc::fx::WorldSettings               world;
    bool                                worldLoaded = false;
    rc::fx::WaterSplashes               splashes;
    rc::fx::ShoreWaves                  shore;
    rc::fx::Gunfire                     gunfire;
    rc::fx::Footprints                  footprints;
    rc::fx::HatchSmoke                  hatch;
    rc::fx::CarEffects::Features        carFeatures;
    bool                                splashOn = true, burstOn = true, solesOn = true, hatchOn = true;
    bool                                shoreOn  = true;

    void tickCar(rc::fx::CarEffects& fx, float dt, const rc::Vehicle& car, const rc::CollisionMesh& track,
                 const rc::Vec3& camera, bool gas, bool brake);
    // `tracks`: the tyre tracks, otherwise the wake and the foam
    void drawMarks(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, const rc::Mat4& viewProj,
                   const rc::Vec3& eye, bool tracks);

    const Tempest::Texture2d*           dust    = nullptr;
    const Tempest::Texture2d*           droplet = nullptr;
    const Tempest::Texture2d*           tyre[4] = {};    // t_halfdry_tire2_<1..4>
    const Tempest::Texture2d*           wake[2] = {};    // wtr_trace, wtr_trace_alpha
    const Tempest::Texture2d*           foam[2] = {};    // whr_horse, whr_horse_alpha
    const Tempest::Texture2d*           wave[2] = {};    // water_wave, water_wave_alpha
    const Tempest::Texture2d*           hole    = nullptr; // bullet_footprint
    const Tempest::Texture2d*           sole    = nullptr; // sole_footprint
    const Tempest::Texture2d*           white   = nullptr;
    Tempest::RenderPipeline             pso;
    Tempest::RenderPipeline             psoTrack, psoWake;
    Tempest::VertexBuffer<Vertex>       vbo[4];   // by the frame in flight and the view (two in the split screen)
    Tempest::VertexBuffer<MarkVertex>   markVbo[2][4];   // [tracks or not][frame and view]
    std::vector<rc::fx::Billboard>      sprites;
    std::vector<Vertex>                 verts;
    std::vector<rc::fx::Mark>           marks;
    std::vector<MarkVertex>             markVerts;
  };
