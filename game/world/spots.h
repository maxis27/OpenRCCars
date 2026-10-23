#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/RenderPipeline>
#include <Tempest/Texture2d>
#include <Tempest/VertexBuffer>

#include <vector>

#include "formats/collision.h"
#include "fx/groundspots.h"
#include "levelmesh.h"

// RCCars.exe's light queue on screen (0x44c640 -> 0x466350): textures laid onto the track around a point
// (rc::fx::layOnto). A shadow takes its texture's colour off what is under it, D3D blend Zero / InvSrcColor (mode 4
// of 0x45c6e0, the textured kind 3 of the queue); a glow adds it.
class SpotView {
  public:
    struct Spot {
      rc::fx::GroundSpot        spot;
      const Tempest::Texture2d* texture = nullptr;
      bool                      glow    = false;
      };

    explicit SpotView(Tempest::Device& device);

    // `frameId`: the frame in flight (0, 1)
    void draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frameId, const std::vector<Spot>& spots,
              const rc::Mat4& viewProj, const LevelMesh::Push& frame, const rc::CollisionMesh& track);

  private:
    struct Vertex {
      float pos[3];
      float uv[2];
      float uv2[2];
      float color[4];
      };
    struct Batch {
      const Tempest::Texture2d* texture = nullptr;
      bool                      glow    = false;
      size_t                    first = 0, count = 0;
      };

    Tempest::Device&                device;
    Tempest::RenderPipeline         psoShadow, psoGlow;
    Tempest::VertexBuffer<Vertex>   vbo[2];
    std::vector<rc::fx::MarkVertex> tris;
    std::vector<Vertex>             verts;
    std::vector<Batch>              batches;
  };
