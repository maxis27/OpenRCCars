#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/Texture2d>
#include <Tempest/VertexBuffer>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "formats/skeleton.h"
#include "physics/characters.h"
#include "levelmesh.h"

class Resources;

// The people and animals of rc::Characters on screen. Their meshes are skinned on the CPU every frame (a few
// thousand vertices for all of them) into world space and drawn with the track's pipelines.
class CharacterView {
  public:
    explicit CharacterView(Tempest::Device& device);
    ~CharacterView();

    // the meshes and textures of all of `chars`, made now rather than when each first comes near the camera (the
    // textures are read from RCCars.pack, which holds up that frame); call it for every new rc::Characters
    void preload(Resources& res, const rc::Characters& chars);
    // `frame` is the frame in flight: its vertex buffer is rewritten. `shades`, by chars.actors(): the
    // rc::fx::FeatureShade of the people and guards (their textures times it, no other light), negative for the
    // animals, which keep the light of the track's models
    void draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, Resources& res,
              const rc::Characters& chars, const LevelMesh::Push& push, const std::vector<float>& shades = {});

  private:
    // one model's triangles, ready to be posed
    struct Model {
      struct Corner {
        float    pos[3];      // skinned: model space in the file's pose; rigid: the mesh's own space
        float    normal[3];
        float    uv[2];
        int32_t  bone = -1;   // rigid: the mesh's bone
        uint8_t  count = 0;   // skinned: bones and weights
        uint16_t bones[4]   = {};
        float    weights[4] = {};
        };
      struct Batch {
        std::string texture;
        size_t      first = 0, count = 0;
        };
      std::vector<Corner> corners;
      std::vector<Batch>  batches;
      };
    struct Draw {
      size_t                    first = 0, count = 0;
      const Tempest::Texture2d* tex   = nullptr;
      rc::AlphaMode             mode  = rc::AlphaMode::Opaque;
      float                     shade = -1;
      };

    const Model& model(const rc::Skeleton& sk);
    // the texture `a` wears for its model's `own` (its "clother" textures)
    static std::string textureOf(const rc::Characters::Actor& a, const std::string& own);

    Tempest::Device&                                   device;
    ScenePipelines                                     pso;
    std::map<const rc::Skeleton*,std::unique_ptr<Model>> models;
    Tempest::VertexBuffer<LevelMesh::Vertex>           vbo[4];   // by the frame in flight and the view (two in the split screen)
    std::vector<LevelMesh::Vertex>                     verts;
    std::vector<Draw>                                  draws;
    std::vector<rc::Mat4>                              pose, skin;
  };
