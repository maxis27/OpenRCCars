#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/RenderPipeline>
#include <Tempest/Texture2d>
#include <Tempest/VertexBuffer>

#include <deque>
#include <vector>

#include "formats/scene.h"
#include "formats/csi.h"

class Resources;

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
      };

    struct Push {
      float    viewProj[16];
      float    cameraPos[4];   // xyz, w = fog distance
      float    sunDir[4];      // xyz, w = lightmap scale
      uint32_t flags = 0;
      uint32_t padding[3] = {};
      };

    struct Stats {
      size_t triangles     = 0;
      size_t batches       = 0;
      size_t skippedMeshes = 0;
      size_t untextured    = 0;
      };

    LevelMesh(Tempest::Device& device, Resources& res, const rc::SbScene& scene);

    void         draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, const Push& frame, bool drawSky = true) const;

    rc::Vec3     bboxMin() const { return bbox[0]; }
    rc::Vec3     bboxMax() const { return bbox[1]; }
    const Stats& stats()   const { return info; }

    static bool  isHiddenMesh(const std::string& name);

  private:
    struct Batch {
      size_t                    first = 0;
      size_t                    count = 0;
      const Tempest::Texture2d* tex[4] = {};
      uint32_t                  flags  = 0;
      rc::AlphaMode             mode   = rc::AlphaMode::Opaque;
      bool                      sky    = false; // sky dome (sky_* textures)
      };

    void                          createPipelines(Tempest::Device& device);
    const Tempest::Texture2d*     sceneTexture(Resources& res, const rc::SbScene& scene, int32_t index, rc::AlphaMode* mode);

    Tempest::VertexBuffer<Vertex> vbo;
    std::vector<Batch>            batches;
    std::deque<Tempest::Texture2d> embedded;  // lightmaps stored inside the scene; deque keeps addresses stable
    std::vector<std::pair<int32_t,size_t>> embeddedIndex;
    Tempest::RenderPipeline       psoOpaque;
    Tempest::RenderPipeline       psoCutout;
    Tempest::RenderPipeline       psoBlend;
    rc::Vec3                      bbox[2];
    Stats                         info;
  };
