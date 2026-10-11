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

// One car from RCCarsDB/Car.sb: MODL "Car1" (Overkill), "Car2" (Buggy) or "Car3" (Hummer).
// Model space: +Z forward, +Y up, about 0.4 m long (radio controlled scale).
// The body and every WHEEL_* node are separate parts, so wheels can spin, steer and move with the suspension.
class CarMesh {
  public:
    struct Part {
      std::string name;   // empty for the body, otherwise the wheel node, e.g. WHEEL_FRONT_LEFT
      rc::Vec3    pivot;  // model space centre of the part (wheel hub); part vertices are relative to it
      };

    CarMesh(Tempest::Device& device, Resources& res, const rc::SbScene& sb, std::string_view model);

    // `world` places the model (rotation + translation only); `partPoses`, if given, places each part in model
    // space (index as in parts(), rotation + translation), default: translate(pivot) - the modelled position
    void                     draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, const rc::Mat4& viewProj,
                                  const LevelMesh::Push& frame, const rc::Mat4& world,
                                  const std::vector<rc::Mat4>* partPoses = nullptr) const;

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
      };

    std::string                           modelName;
    std::vector<Part>                     partList;
    std::vector<Batch>                    batches;
    Tempest::VertexBuffer<LevelMesh::Vertex> vbo;
    ScenePipelines                        pso;
    rc::Vec3                              bbox[2];
  };
