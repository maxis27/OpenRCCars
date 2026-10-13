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

// The cars' particle effects (fx/careffects.h): dust behind the wheels and spray in water, for the player's car and
// the opponents, drawn as sprites facing the camera after the translucent pass of the track, sorted far to near,
// tested against the scene's depth without writing it.
class Effects {
  public:
    Effects(Tempest::Device& device, Resources& res);

    // all particles gone (a new race, the car put back)
    void clear();
    // after the physics; `gas`/`brake`: the player's keys
    void tick(float dt, const rc::Vehicle& player, const rc::Opponents* opponents, const rc::CollisionMesh& track,
              const rc::Vec3& camera, bool gas, bool brake);
    // `eye`: the camera's position
    void draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, int w, int h, const rc::Mat4& viewProj,
              const rc::Vec3& eye);

  private:
    struct Vertex {
      float pos[2];
      float uv[2];
      float color[4];
      float depth = 0;
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
    std::vector<Opponent>               opponents;

    const Tempest::Texture2d*           dust    = nullptr;
    const Tempest::Texture2d*           droplet = nullptr;
    Tempest::RenderPipeline             pso;
    Tempest::VertexBuffer<Vertex>       vbo[2];
    std::vector<rc::fx::Billboard>      sprites;
    std::vector<Vertex>                 verts;
  };
