#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/RenderPipeline>
#include <Tempest/Texture2d>
#include <Tempest/UniformBuffer>
#include <Tempest/VertexBuffer>

#include <deque>
#include <memory>
#include <string_view>
#include <vector>

#include "formats/collision.h"
#include "formats/scene.h"
#include "formats/csi.h"
#include "formats/pack.h"
#include "formats/water.h"

class Resources;

// shader/common.glsl "Water" block (binding 4): the waves of one sea (formats/water.h) and the shore foam
struct WaterBlock {
  float wave[4]  = {};  // amp, period, speed, amp2
  float wave2[4] = {};  // period2, 1/length2, center x, center z
  float dir[4]   = {};  // cos(angle), sin(angle), offset, magnetOffset
  float edge[4]  = {};  // 1/magnetRadius, alphaRadius, distAlphaClampMin, distAlphaClampMax
  float color[4] = {};  // colorMin/255, colorMax/255, 1 = waves set, foam heightOn
  float foam[4]  = {};  // foam alphaMin, alphaMax, scrollVel
  float detail[4] = {}; // the detail texture (F_Detail): scale, density/256, mip bias

  static WaterBlock from(const rc::WaterWaves& w, const rc::CoastFoam& foam);
  };

// Pipelines of shader/level.*, shared by the track and the cars.
struct ScenePipelines {
  explicit ScenePipelines(Tempest::Device& device);
  const Tempest::RenderPipeline& get(rc::AlphaMode mode) const;

  Tempest::RenderPipeline opaque;
  Tempest::RenderPipeline cutout;
  Tempest::RenderPipeline blend;
  Tempest::RenderPipeline modulate; // twice the source times the destination
  // the sea, split up by tessellation to move it with the waves; empty when the GPU can't tessellate
  Tempest::RenderPipeline water;
  // binding 4 of everything but the sea
  Tempest::UniformBuffer<WaterBlock> noWater;

  // binding 5: the environment map, one face of the cube map "cubemap_racer" (RCCars.exe 0x48f530 loads it as the
  // default for every mesh that has one); white if it is missing
  static const Tempest::Texture2d& environment(Resources& res);
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
      F_Waves    = 32, // water with WLOD waves (formats/water.h), uvMask.x = distance to the edge of the sea
      F_Tess     = 64, // drawn with ScenePipelines::water: the tessellation moves the vertices
      F_Dirt     = 128, // ModelMesh: the car's dirt over its paint, `time` = how dirty
      F_Modulate = 256, // multiplied onto what is under it (SbMesh::renderFlags 0x40000000)
      F_CarLight = 512, // ModelMesh::setLight: ambient carPos.w + direct sunDir.w towards sunDir.xyz, held at 1
      F_Detail   = 1024, // the detail texture (LevelMesh::Detail) in binding 5, its settings in the Water block
      };

    // RCCars.exe's "detailer" (Settings\Detail.ini, loaded by 0x480a70; VIDEO_Detailer, on by default): six detail
    // textures (detailer, detailer2 .. detailer6) laid over the meshes near the camera. Which one a texture gets
    // comes from its GLTX groups (SbTexture::groups, see detailLayer in levelmesh.cpp)
    struct Detail {
      float density[6] = {118,118,110,96,256,118}; // the sliders as they are, /256 is the strength
      float lodBias[6] = {37,16,7,20,51,50};       // mip bias v*0.04 - 2
      float scale[6]   = {100,100,30,89,101,60};   // repeats over the base texture: v*0.1
      float distObj    = 30;                       // v*0.1 m: only meshes whose box is that close to the camera
      static Detail load(rc::PackFile& pack);
      };

    struct Push {
      float    viewProj[16];
      float    cameraPos[4];   // xyz, w = fog density (rc::Fog)
      float    sunDir[4];      // xyz, w = lightmap scale
      uint32_t flags = 0;
      float    time = 0;       // seconds, animates the water and the foam
      float    envMap = 0;     // strength of the environment map (ScenePipelines::env), 0 = none
      uint32_t fogColor = 0;  // 0x00RRGGBB
      float    carPos[4] = {0,1e6f,0,0}; // the player's car: the sea is see-through near it (WaterWaves)
      };

    struct Stats {
      size_t triangles     = 0;
      size_t batches       = 0;
      size_t skippedMeshes = 0;
      size_t untextured    = 0;
      };

    // `ground` (the track collision) gives water without WLOD settings its depth, so that it fades out towards
    // the shore; `track` names the WLOD_<track>_<object>.ini files of the sea
    // `groups`: the moving scenery (rc::fx::AnimatedObjects::groups), the group each of the scene's meshes goes
    // with, 0 for those that stay
    LevelMesh(Tempest::Device& device, Resources& res, const rc::SbScene& scene, const rc::CollisionMesh* ground = nullptr,
              std::string_view track = {}, const std::vector<int32_t>* groups = nullptr);


    // opaque and alpha-tested batches first, then (after the cars) the see-through ones: water, shore foam ...
    enum class Pass { Opaque, Translucent };
    void         draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, const Push& frame, Pass pass, bool drawSky = true) const;
    // the same with the moving scenery placed by `poses` (rc::fx::AnimatedObjects::poses, index = group) on top of
    // `viewProj` (the matrix in `frame`)
    void         draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, const Push& frame, Pass pass,
                      const rc::Mat4& viewProj, const std::vector<rc::Mat4>& poses, bool drawSky = true) const;

    rc::Vec3     bboxMin() const { return bbox[0]; }
    rc::Vec3     bboxMax() const { return bbox[1]; }
    const Stats& stats()   const { return info; }

    // how far the waves lift the sea that `mesh` (SbScene::meshes) belongs to above its still level
    // (CollisionMesh::waterSurface) at `p`, `time` seconds in, as the shaders move it; 0 for water without waves
    float        waveLift(uint32_t mesh, const rc::Vec3& p, float time) const;
    // where the sea sound may sit: the corners of the outline of the track's sea (the last one, as RCCars.exe has
    // it), at the height of that sea's offset; empty without a sea
    const std::vector<rc::Vec3>& seaCoast() const;

    // the waves of the main sea (the node "more1", else the first sea with waves), null if none
    const rc::WaterWaves* mainSea() const;

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
      int32_t                   water  = -1;    // waterUbo, -1: ScenePipelines::noWater
      float                     envMap = 0;     // Push::envMap
      int32_t                   group  = 0;     // the moving scenery it goes with, 0 none
      int32_t                   detail = 0;     // detail texture 1..6, 0 none
      // detail batches: the part of each mesh, drawn with the detail texture only when it is near the camera
      struct Range { size_t first = 0, count = 0; rc::Vec3 lo, hi; };
      std::vector<Range>        ranges;
      };
    struct SeaSurface; // the seas' waves and outlines, for waveLift

    const Tempest::Texture2d*     sceneTexture(Resources& res, const rc::SbScene& scene, int32_t index, int32_t model, rc::AlphaMode* mode);

    Tempest::VertexBuffer<Vertex> vbo;
    std::vector<Batch>            batches;
    std::vector<Tempest::UniformBuffer<WaterBlock>> waterUbo; // one per sea with waves
    Tempest::UniformBuffer<WaterBlock> detailUbo[6];           // the settings of each detail texture
    const Tempest::Texture2d*     detailTex[6] = {};
    float                         detailDist   = 3;
    std::deque<Tempest::Texture2d> embedded;  // lightmaps stored inside the scene; deque keeps addresses stable
    std::vector<std::pair<int32_t,size_t>> embeddedIndex;
    ScenePipelines                pso;
    const Tempest::Texture2d*     env = nullptr;
    std::shared_ptr<SeaSurface>   seaSurface;
    rc::Vec3                      bbox[2];
    Stats                         info;
  };
