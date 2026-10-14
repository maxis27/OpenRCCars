#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/Texture2d>
#include <Tempest/VertexBuffer>

#include <string>
#include <string_view>
#include <vector>

#include "formats/scene.h"
#include "levelmesh.h"

class Resources;

// One MODL of a scene on the GPU: a car from RCCarsDB/Car.sb ("Car1" Overkill, "Car2" Buggy, "Car3" Hummer;
// +Z forward, +Y up, about 0.4 m long) or an object placed on a track (stone.sb, people.sb ...).
// The body, every WHEEL_* node and the suspension's nodes are separate parts (rc::carNode), so wheels can spin
// and steer and the axles, arms and springs can move (rc::CarRig).
class ModelMesh {
  public:
    struct Part {
      std::string name;   // empty for the body, otherwise the node, e.g. WHEEL_FRONT_LEFT or FRONT_AXLE
      rc::Vec3    pivot;  // model space centre of the part (a wheel's hub); part vertices are relative to it
      };

    // `origin`: model space point that becomes the origin (ObjectLibrary::Model::origin). `skin` > 0 paints a car
    // with another of its liveries: Textures*/car_askin<car><1..4>.csi (and car_bskin..), the model using 1;
    // skin 1 is the second one and so on (a guess: Scripts/ailayouts.ini gives the opponents skins 1 .. 3)
    ModelMesh(Tempest::Device& device, Resources& res, const rc::SbScene& sb, std::string_view model,
              const rc::Vec3& origin = {}, int skin = 0);

    // `world` places the model (rotation, translation and a uniform scale); `partPoses`, if given, places each part in model
    // space (index as in parts(); rotation, translation, a spring's stretch), default: translate(pivot) - the modelled
    // position
    void                     draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, const rc::Mat4& viewProj,
                                  const LevelMesh::Push& frame, const rc::Mat4& world,
                                  const std::vector<rc::Mat4>* partPoses = nullptr) const;

    // how dirty the car is, 0 clean .. 2 (Effects::dirt): its paint (car_askin<car>*, car_bskin<car>*) gets the
    // dirt_car<car>_1/_2 textures (with a/b for the two paints of cars 2 and 3) laid over it
    void                     setDirt(float level) { dirt = level; }

    const std::string&       name()    const { return modelName; }
    const std::vector<Part>& parts()   const { return partList; }
    rc::Vec3                 bboxMin() const { return bbox[0]; }
    rc::Vec3                 bboxMax() const { return bbox[1]; }

  private:
    struct Batch {
      size_t                    part  = 0;
      size_t                    first = 0;
      size_t                    count = 0;
      const Tempest::Texture2d* tex   = nullptr;
      rc::AlphaMode             mode  = rc::AlphaMode::Opaque;
      const Tempest::Texture2d* dirt[2] = {};
      float                     envMap = 0; // LevelMesh::Push::envMap
      };

    std::string                           modelName;
    std::vector<Part>                     partList;
    std::vector<Batch>                    batches;
    Tempest::VertexBuffer<LevelMesh::Vertex> vbo;
    ScenePipelines                        pso;
    const Tempest::Texture2d*             envTex = nullptr;
    rc::Vec3                              bbox[2];
    float                                 dirt = 0;
  };
