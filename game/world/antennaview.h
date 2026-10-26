#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/Texture2d>
#include <Tempest/VertexBuffer>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "formats/skeleton.h"
#include "levelmesh.h"
#include "modelmesh.h"

class Resources;

// The cars' springy antennas (rc::Antenna) on screen. RCCars.exe hides the ANTENNA node of the car's full model
// (ModelMesh::CarLook::antenna) and draws Car<n>Antenna of Car.sb instead: the mesh Antennaa skinned to the joints
// jnt3_1 .. jnt3_3, which the chain of rc::Antenna places. All antennas of a frame are skinned on the CPU into world
// space and drawn together.
class AntennaView {
  public:
    AntennaView(Tempest::Device& device, Resources& res, const rc::SbScene& carScene);
    ~AntennaView();

    // the car (0..2) has an antenna of its own
    bool     has(size_t car) const;
    // what rc::Antenna stands on: the mount (car space, rc::Antenna::mountOf of the car's model) and the rotation
    // of the first point's frame (the same for all cars, whatever the joints' bind pose: Car2's antenna is modelled
    // bent back, the chain starts straight up)
    rc::Vec3 mount(size_t car) const;
    rc::Mat4 rest(size_t car) const;
    // the joints as modelled, standing on the car at `carPose`
    std::vector<rc::Mat4> restJoints(size_t car, const rc::Mat4& carPose) const;

    // starts a frame's antennas
    void clear();
    // one antenna with its joints (rc::Antenna::joints), lit as `look` (the car's full model)
    void add(size_t car, const std::vector<rc::Mat4>& joints, const ModelMesh::Light& look);
    // `frame` is the frame in flight: its vertex buffer is rewritten
    void draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, const LevelMesh::Push& push);

  private:
    struct Corner {
      rc::Vec3 pos, normal;   // car space, in the pose the skin was bound in
      float    uv[2] = {};
      uint8_t  count = 0;
      uint8_t  joint[4]   = {};
      float    weight[4]  = {};
      };
    struct Model {
      std::unique_ptr<rc::Skeleton> skeleton;
      rc::Vec3                      mount;
      std::vector<rc::Mat4>         bind, bindInverse;  // of the joints
      std::vector<Corner>           corners;
      const Tempest::Texture2d*     tex  = nullptr;
      rc::AlphaMode                 mode = rc::AlphaMode::Opaque;
      };
    struct Draw {
      size_t                 car = 0, first = 0, count = 0;
      ModelMesh::Light       light;
      };

    Tempest::Device&                         device;
    Resources&                               res;
    ScenePipelines                           pso;
    std::array<Model,3>                      models;
    Tempest::VertexBuffer<LevelMesh::Vertex> vbo[2];
    std::vector<LevelMesh::Vertex>           verts;
    std::vector<Draw>                        draws;
  };
