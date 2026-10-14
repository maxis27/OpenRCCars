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

class Resources;

namespace rc {
class CollisionMesh;
class Opponents;
class Vehicle;
}

// The cars' effects (fx/careffects.h), for the player's car and the opponents: dust behind the wheels, spray in water
// and exhaust smoke, drawn as sprites facing the camera, sorted far to near; tyre tracks, the wake and the foam as
// quads in the world: the tracks before the translucent pass of the track, the rest after it. All tested against the
// scene's depth without writing it.
class Effects {
  public:
    Effects(Tempest::Device& device, Resources& res);

    // all gone (a new race); with `keep` (the car put back) the particles only: the tracks stay, and the dirt
    void clear(bool keep = false);
    // how dirty the player's car is: 0 clean .. 2 (CarEffects::dirt)
    float dirt() const;
    // after the physics; `gas`/`brake`: the player's keys
    void tick(float dt, const rc::Vehicle& player, const rc::Opponents* opponents, const rc::CollisionMesh& track,
              const rc::Vec3& camera, bool gas, bool brake);
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
    float                               fogDensity = 0;
    uint32_t                            fogColor   = 0;
    std::vector<Opponent>               opponents;

    // `tracks`: the tyre tracks, otherwise the wake and the foam
    void drawMarks(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, const rc::Mat4& viewProj,
                   const rc::Vec3& eye, bool tracks);

    const Tempest::Texture2d*           dust    = nullptr;
    const Tempest::Texture2d*           droplet = nullptr;
    const Tempest::Texture2d*           tyre[4] = {};    // t_halfdry_tire2_<1..4>
    const Tempest::Texture2d*           wake[2] = {};    // wtr_trace, wtr_trace_alpha
    const Tempest::Texture2d*           foam[2] = {};    // whr_horse, whr_horse_alpha
    Tempest::RenderPipeline             pso;
    Tempest::RenderPipeline             psoTrack, psoWake;
    Tempest::VertexBuffer<Vertex>       vbo[2];
    Tempest::VertexBuffer<MarkVertex>   markVbo[2][2];   // [tracks or not][frame]
    std::vector<rc::fx::Billboard>      sprites;
    std::vector<Vertex>                 verts;
    std::vector<rc::fx::Mark>           marks;
    std::vector<MarkVertex>             markVerts;
  };
