#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/RenderPipeline>
#include <Tempest/VertexBuffer>

#include "formats/collision.h"
#include "levelmesh.h"

// Debug overlay of the track collision: translucent triangles with outlined edges, drawn over the level.
//   green - ground (up to ~35 degrees), yellow - slopes, red - walls, blue - faces without a texture
//   (invisible in the game, e.g. barriers that keep the car on the track).
class CollisionView {
  public:
    struct Vertex {
      float pos[3];
      float bary[3];
      float color[4];
      };

    CollisionView(Tempest::Device& device, const rc::CollisionMesh& coll, const rc::SbScene& scene);

    void draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, const LevelMesh::Push& frame) const;

  private:
    Tempest::VertexBuffer<Vertex> vbo;
    Tempest::RenderPipeline       pso;
  };
