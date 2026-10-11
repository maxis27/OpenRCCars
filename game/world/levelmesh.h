#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/RenderPipeline>
#include <Tempest/Texture2d>
#include <Tempest/VertexBuffer>

#include <deque>
#include <vector>

#include "formats/collision.h"
#include "formats/scene.h"
#include "formats/csi.h"

class Resources;

// Pipelines of shader/level.*, shared by the track and the cars.
struct ScenePipelines {
  explicit ScenePipelines(Tempest::Device& device);
  const Tempest::RenderPipeline& get(rc::AlphaMode mode) const;

  Tempest::RenderPipeline opaque;
  Tempest::RenderPipeline cutout;
  Tempest::RenderPipeline blend;
  // the sea, split up by tessellation to move it with the waves; empty when the GPU can't tessellate
  Tempest::RenderPipeline water;
  };

// Static, pre-transformed geometry of a .sb scene, grouped into batches by texture layers.
class LevelMesh {
  public:
    struct Vertex {
      float pos[3];
      float normal[3];
      float uvBase[2];
      float uvLight[2];
      float uvMask[2];
      float uvBase2[2];
      };

    enum Flags : uint32_t {
      F_Lightmap = 1, // keep in sync with shader/common.glsl
      F_Blend    = 2,
      F_Cutout   = 4,
      F_Mask     = 8,
      F_Water    = 16,
      };

    struct Push {
      float    viewProj[16];
      float    cameraPos[4];   // xyz, w = fog distance
      float    sunDir[4];      // xyz, w = lightmap scale
      uint32_t flags = 0;
      float    time = 0;       // seconds, animates the water and the foam
      uint32_t padding[2] = {};
      };

    struct Stats {
      size_t triangles     = 0;
      size_t batches       = 0;
      size_t skippedMeshes = 0;
      size_t untextured    = 0;
      };

    // `ground` (the track collision) gives the water its depth, so that the sea fades out towards the shore
    LevelMesh(Tempest::Device& device, Resources& res, const rc::SbScene& scene, const rc::CollisionMesh* ground = nullptr);

    // opaque and alpha-tested batches first, then (after the cars) the see-through ones: water, shore foam ...
    enum class Pass { Opaque, Translucent };
    void         draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, const Push& frame, Pass pass, bool drawSky = true) const;

    rc::Vec3     bboxMin() const { return bbox[0]; }
    rc::Vec3     bboxMax() const { return bbox[1]; }
    const Stats& stats()   const { return info; }

    // low detail copies (_LOD<n>) that are not drawn, per SbScene::meshes
    static std::vector<bool> hiddenMeshes(const rc::SbScene& scene);

  private:
    struct Batch {
      size_t                    first = 0;
      size_t                    count = 0;
      const Tempest::Texture2d* tex[4] = {};
      uint32_t                  flags  = 0;
      rc::AlphaMode             mode   = rc::AlphaMode::Opaque;
      bool                      sky    = false; // sky dome (sky_* textures)
      };

    const Tempest::Texture2d*     sceneTexture(Resources& res, const rc::SbScene& scene, int32_t index, int32_t model, rc::AlphaMode* mode);

    Tempest::VertexBuffer<Vertex> vbo;
    std::vector<Batch>            batches;
    std::deque<Tempest::Texture2d> embedded;  // lightmaps stored inside the scene; deque keeps addresses stable
    std::vector<std::pair<int32_t,size_t>> embeddedIndex;
    ScenePipelines                pso;
    rc::Vec3                      bbox[2];
    Stats                         info;
  };
