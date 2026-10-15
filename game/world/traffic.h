#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/RenderPipeline>
#include <Tempest/VertexBuffer>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "formats/collision.h"
#include "fx/marks.h"
#include "physics/traffic.h"
#include "modelmesh.h"

class Resources;

// The road traffic of rc::Traffic on screen: one ModelMesh per model and livery, its wheels, the BTR's turret and
// the head lights posed by rc::Traffic; then the wheels' shadows and the head lights' glow laid onto the track
// (rc::Traffic::Spot, rc::fx::layOnto).
class TrafficView {
  public:
    TrafficView(Tempest::Device& device, Resources& res);
    ~TrafficView();

    // `track`: the faces the spots are laid onto (none: no spots); `frame`: the frame in flight (0, 1)
    void draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frameId, const rc::Traffic& traffic,
              const rc::Mat4& viewProj, const LevelMesh::Push& frame, const rc::CollisionMesh* track);

  private:
    using Key = std::pair<const rc::ObjectLibrary::Model*,std::vector<std::pair<std::string,std::string>>>;
    const ModelMesh* mesh(const rc::Traffic::Car& car);
    void             drawSpots(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frameId,
                               const rc::Traffic& traffic, const rc::Mat4& viewProj, const LevelMesh::Push& frame,
                               const rc::CollisionMesh& track);

    struct Vertex {
      float pos[3];
      float uv[2];
      float uv2[2];
      float color[4];
      };

    Tempest::Device&                            device;
    Resources&                                  res;
    std::map<Key,std::unique_ptr<ModelMesh>>    meshes;
    const Tempest::Texture2d*                   shadowTex = nullptr;  // wheel_shd
    const Tempest::Texture2d*                   glowTex   = nullptr;  // shex_wht
    Tempest::RenderPipeline                     psoShadow, psoGlow;
    Tempest::VertexBuffer<Vertex>               vbo[2];
    std::vector<rc::fx::MarkVertex>             tris[2];               // shadows, glow
    std::vector<Vertex>                         verts;
  };
